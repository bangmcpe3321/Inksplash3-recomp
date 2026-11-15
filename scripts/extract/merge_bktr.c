/* merge_bktr.c - merge a Splatoon 3 (v11.3.0) BKTR patch romfs with its base romfs.
 *
 * usage: merge_bktr <update.nca> <base.nca> <update-key> <base-key>
 *                   <reloc-tree.bin> <enc-tree.bin> <base-sparse-table.bin> <out.img>
 *
 * Layout facts (fixed for this title, printed by ncainfo):
 *   update romfs section: start 0x4000, encryption AesCtrEx,
 *       patch info: reloc tree @0x1AD9E3600 (0x24000), enc tree @0x1ADA07600 (0x18000)
 *       fs header generation/SecureValue = 0x29 / 2
 *   base   romfs section: start 0x1C000, encryption AesCtr, SparseInfo
 *       fs header generation/SecureValue = 0x0 / 2
 *
 * Resolution:
 *   relocation entries give {virt, phys, is_patch}; region length is the distance to
 *   the next entry (last one ends at the tree's total size).
 *     is_patch=1 -> patch storage at update-section-relative `phys`, decrypted with
 *                   AesCtrEx: iv = BE64(gen|secure<<32) || BE64((0x4000+phys)/0x10)
 *                   where `gen` is the encryption-tree entry covering that offset.
 *     is_patch=0 -> base section at offset `phys`, resolved through the base sparse
 *                   table, decrypted with plain CTR at (0x1C000+off)/0x10.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mbedtls/aes.h"

#define BUFSZ (8u * 1024u * 1024u)
#define UPDATE_SEC_START 0x4000ULL
#define BASE_SEC_START   0x1C000ULL

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int unhex(const char *s, unsigned char *out, size_t max) {
    size_t n = strlen(s);
    if (n % 2 || n / 2 > max) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return (int)(n / 2);
}
static uint64_t rd64(const unsigned char *p) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static unsigned char *slurp(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fprintf(stderr, "read %s failed\n", path); exit(1); }
    fclose(f);
    *out_size = (size_t)sz;
    return b;
}

/* CTR decrypt buf (16-byte aligned, len multiple of 16). */
static void ctr_decrypt(mbedtls_aes_context *aes, unsigned char *buf, size_t len,
                        uint64_t upper, uint64_t byte_offset) {
    unsigned char ctr[16], ks[16];
    for (int i = 0; i < 8; i++) ctr[i] = (unsigned char)((upper >> (56 - 8 * i)) & 0xFF);
    uint64_t blk = byte_offset / 0x10;
    for (int i = 0; i < 8; i++) ctr[8 + i] = (unsigned char)((blk >> (56 - 8 * i)) & 0xFF);
    for (size_t i = 0; i < len; i += 16) {
        mbedtls_aes_crypt_ecb(aes, MBEDTLS_AES_ENCRYPT, ctr, ks);
        for (int j = 15; j >= 8; j--) { if ((++ctr[j]) != 0) break; }
        size_t n = (len - i < 16) ? (len - i) : 16;
        for (size_t j = 0; j < n; j++) buf[i + j] ^= ks[j];
    }
}

typedef struct { uint64_t virt, phys; uint32_t is_patch; } reloc_t;
typedef struct { int64_t off; int32_t gen; } enc_t;
typedef struct { uint64_t virt, phys; uint32_t zero; } sreg_t;

int main(int argc, char **argv) {
    if (argc != 9) {
        fprintf(stderr, "usage: %s <update.nca> <base.nca> <update-key> <base-key> <reloc> <enc> <base-sparse> <out>\n", argv[0]);
        return 2;
    }
    unsigned char ukey[16], bkey[16];
    if (unhex(argv[3], ukey, 16) != 16 || unhex(argv[4], bkey, 16) != 16) { fprintf(stderr, "bad key\n"); return 2; }

    size_t rsz, esz, ssz;
    unsigned char *rb = slurp(argv[5], &rsz);
    unsigned char *eb = slurp(argv[6], &esz);
    unsigned char *sb = slurp(argv[7], &ssz);

    /* relocation tree */
    uint32_t rnb = rd32(rb + 4);
    uint64_t total = rd64(rb + 8);
    size_t rcount = 0;
    reloc_t *rel = malloc(0x20000 * sizeof(reloc_t));
    for (uint32_t b = 0; b < rnb; b++) {
        const unsigned char *blk = rb + 0x4000 + (size_t)b * 0x4000;
        uint32_t ne = rd32(blk + 4);
        for (uint32_t i = 0; i < ne; i++) {
            const unsigned char *e = blk + 0x10 + (size_t)i * 0x14;
            rel[rcount].virt = rd64(e);
            rel[rcount].phys = rd64(e + 8);
            rel[rcount].is_patch = rd32(e + 0x10);
            rcount++;
        }
    }

    /* encryption tree */
    uint32_t enb = rd32(eb + 4);
    size_t ecount = 0;
    enc_t *enc = malloc(0x10000 * sizeof(enc_t));
    for (uint32_t b = 0; b < enb; b++) {
        const unsigned char *blk = eb + 0x4000 + (size_t)b * 0x4000;
        uint32_t ne = rd32(blk + 4);
        for (uint32_t i = 0; i < ne; i++) {
            const unsigned char *e = blk + 0x10 + (size_t)i * 0x10;
            enc[ecount].off = (int64_t)rd64(e);
            enc[ecount].gen = (int32_t)rd32(e + 0x0C);
            ecount++;
        }
    }

    /* base sparse table */
    uint32_t snb = rd32(sb + 4);
    size_t scount = 0;
    sreg_t *sreg = malloc(0x1000 * sizeof(sreg_t));
    for (uint32_t b = 0; b < snb; b++) {
        const unsigned char *blk = sb + 0x4000 + (size_t)b * 0x4000;
        uint32_t ne = rd32(blk + 4);
        for (uint32_t i = 0; i < ne; i++) {
            const unsigned char *e = blk + 0x10 + (size_t)i * 0x14;
            sreg[scount].virt = rd64(e);
            sreg[scount].phys = rd64(e + 8);
            sreg[scount].zero = rd32(e + 0x10);
            scount++;
        }
    }

    printf("merged total=0x%llx reloc=%zu enc=%zu sparse=%zu\n",
           (unsigned long long)total, rcount, ecount, scount);

    FILE *uf = fopen(argv[1], "rb");
    FILE *bf = fopen(argv[2], "rb");
    FILE *of = fopen(argv[8], "wb");
    if (!uf || !bf || !of) { perror("open"); return 1; }

    mbedtls_aes_context uaes, baes;
    mbedtls_aes_init(&uaes); mbedtls_aes_setkey_enc(&uaes, ukey, 128);
    mbedtls_aes_init(&baes); mbedtls_aes_setkey_enc(&baes, bkey, 128);

    unsigned char *buf = malloc(BUFSZ);
    if (!buf) { fprintf(stderr, "oom\n"); return 1; }

    uint64_t pos = 0;
    size_t ri = 0;
    uint64_t patchBytes = 0, baseBytes = 0;
    while (pos < total) {
        while (ri + 1 < rcount && rel[ri + 1].virt <= pos) ri++;
        uint64_t regionEnd = (ri + 1 < rcount) ? rel[ri + 1].virt : total;
        uint64_t regionOff = pos - rel[ri].virt;

        size_t chunk = (size_t)((regionEnd - pos < BUFSZ) ? (regionEnd - pos) : BUFSZ);

        if (rel[ri].is_patch) {
            uint64_t phys = rel[ri].phys + regionOff;
            size_t ei = 0;
            {   /* binary search: last entry with off <= phys */
                size_t lo = 0, hi = ecount;
                while (lo + 1 < hi) { size_t mid = (lo + hi) / 2;
                    if ((uint64_t)enc[mid].off <= phys) lo = mid; else hi = mid; }
                ei = lo;
            }
            /* clamp to the encryption entry boundary */
            if (ei + 1 < ecount) {
                uint64_t lim = (uint64_t)enc[ei + 1].off - phys;
                if (lim < chunk) chunk = (size_t)lim;
            }
            fseeko(uf, (off_t)(UPDATE_SEC_START + phys), SEEK_SET);
            if (fread(buf, 1, chunk, uf) != chunk) { fprintf(stderr, "short read update @0x%llx\n", (unsigned long long)phys); return 1; }
            uint64_t upper = (uint64_t)(uint32_t)enc[ei].gen | (2ULL << 32);
            ctr_decrypt(&uaes, buf, chunk, upper, UPDATE_SEC_START + phys);
            patchBytes += chunk;
        } else {
            uint64_t bvirt = rel[ri].phys + regionOff;
            size_t si = 0;
            {   /* binary search: last region with virt <= bvirt */
                size_t lo = 0, hi = scount;
                while (lo + 1 < hi) { size_t mid = (lo + hi) / 2;
                    if (sreg[mid].virt <= bvirt) lo = mid; else hi = mid; }
                si = lo;
            }
            uint64_t lim = ((si + 1 < scount) ? sreg[si + 1].virt : (uint64_t)-1) - bvirt;
            if (lim < chunk) chunk = (size_t)lim;
            if (sreg[si].zero) {
                memset(buf, 0, chunk);
            } else {
                uint64_t foff = BASE_SEC_START + sreg[si].phys + (bvirt - sreg[si].virt);
                fseeko(bf, (off_t)foff, SEEK_SET);
                if (fread(buf, 1, chunk, bf) != chunk) { fprintf(stderr, "short read base @0x%llx\n", (unsigned long long)foff); return 1; }
            }
            ctr_decrypt(&baes, buf, chunk, 2ULL << 32, BASE_SEC_START + bvirt);
            baseBytes += chunk;
        }

        fseeko(of, (off_t)pos, SEEK_SET);
        if (fwrite(buf, 1, chunk, of) != chunk) { perror("write"); return 1; }
        pos += chunk;
    }

    printf("done: total=0x%llx patch=0x%llx base=0x%llx\n",
           (unsigned long long)pos, (unsigned long long)patchBytes, (unsigned long long)baseBytes);
    fclose(of); fclose(uf); fclose(bf);
    mbedtls_aes_free(&uaes); mbedtls_aes_free(&baes);
    free(buf); free(rel); free(enc); free(sreg); free(rb); free(eb); free(sb);
    return 0;
}
