/* desparse.c - de-sparse + decrypt an NCA sparse section into a plain image.
 *
 * usage: desparse <content-key-hex> <nca> <sparse-table.bin> <out.bin> <phys-base> <upper-hex16> <fs-offset-base>
 *
 * <sparse-table.bin> is the already-CTR-decrypted bucket tree of the section
 * (node block + bucket blocks). <phys-base> is the absolute offset in the NCA
 * where the section's sparse storage starts (sparse_info.physical_offset).
 *
 * The output is the section's virtual image, AES-CTR decrypted with
 * IV = BE64(upper) || BE64(virtual_offset / 0x10), which is what hactool's
 * "-t romfs" / NCA section handling expects.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mbedtls/aes.h"

#define BUFSZ (4u * 1024u * 1024u)

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
static uint64_t rd64(const unsigned char *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}
static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

typedef struct {
    uint64_t virt;
    uint64_t phys;
    uint32_t flags; /* 1 = zero region, 0 = data */
} region_t;

int main(int argc, char **argv) {
    if (argc != 8) {
        fprintf(stderr, "usage: %s <key> <nca> <table.bin> <out.bin> <phys-base> <upper-hex16> <fs-offset-base>\n", argv[0]);
        return 2;
    }
    unsigned char key[16], upper[8];
    if (unhex(argv[1], key, 16) != 16 || unhex(argv[6], upper, 8) != 8) {
        fprintf(stderr, "bad key/upper\n");
        return 2;
    }
    uint64_t physBase = strtoull(argv[5], NULL, 0);
    uint64_t fsOffset = strtoull(argv[7], NULL, 0);

    FILE *tf = fopen(argv[3], "rb");
    if (!tf) { perror("table"); return 1; }
    fseek(tf, 0, SEEK_END);
    long tsize = ftell(tf);
    fseek(tf, 0, SEEK_SET);
    unsigned char *tbl = malloc((size_t)tsize);
    if (!tbl || fread(tbl, 1, (size_t)tsize, tf) != (size_t)tsize) { perror("table read"); return 1; }
    fclose(tf);

    if (memcmp(tbl, "\0\0\0\0", 4) != 0) { fprintf(stderr, "unexpected node block\n"); return 1; }
    uint32_t nbuckets = rd32(tbl + 4);
    uint64_t total = rd64(tbl + 8);
    printf("table: buckets=%u total=0x%llx\n", nbuckets, (unsigned long long)total);

    /* Parse bucket 0 (single-bucket case, which this NCA uses). */
    const unsigned char *b0 = tbl + 0x4000;
    uint32_t nent = rd32(b0 + 4);
    printf("bucket0: entries=%u end=0x%llx\n", nent, (unsigned long long)rd64(b0 + 8));

    region_t *regs = calloc(nent + 1, sizeof(*regs));
    if (!regs) { fprintf(stderr, "oom\n"); return 1; }
    for (uint32_t i = 0; i < nent; i++) {
        const unsigned char *e = b0 + 0x10 + (size_t)i * 0x14;
        regs[i].virt = rd64(e);
        regs[i].phys = rd64(e + 8);
        regs[i].flags = rd32(e + 0x10);
    }

    FILE *nf = fopen(argv[2], "rb");
    if (!nf) { perror("nca"); return 1; }
    FILE *of = fopen(argv[4], "wb");
    if (!of) { perror("out"); return 1; }

    unsigned char *buf = malloc(BUFSZ);
    if (!buf) { fprintf(stderr, "oom\n"); return 1; }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);

    unsigned char ctr[16], ks[16];
    uint64_t pos = 0;
    int ri = 0;
    uint64_t zeroBytes = 0, dataBytes = 0;
    while (pos < total) {
        /* Advance region index so that regs[ri].virt <= pos < next.virt */
        while (ri + 1 < (int)nent && regs[ri + 1].virt <= pos) ri++;
        uint64_t regionEnd = (ri + 1 < (int)nent) ? regs[ri + 1].virt : total;
        size_t chunk = (size_t)((regionEnd - pos < BUFSZ) ? (regionEnd - pos) : BUFSZ);

        if (regs[ri].flags == 1) {
            memset(buf, 0, chunk);
            zeroBytes += chunk;
        } else {
            uint64_t foff = physBase + regs[ri].phys + (pos - regs[ri].virt);
            fseeko(nf, (off_t)foff, SEEK_SET);
            if (fread(buf, 1, chunk, nf) != chunk) { fprintf(stderr, "short read at 0x%llx\n", (unsigned long long)foff); return 1; }
            dataBytes += chunk;
        }

        /* CTR decrypt for this chunk */
        memcpy(ctr, upper, 8);
        uint64_t blk = (fsOffset + pos) / 0x10;
        for (int i = 7; i >= 0; i--) { ctr[8 + i] = (unsigned char)(blk & 0xFF); blk >>= 8; }
        for (size_t i = 0; i < chunk; i += 16) {
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, ctr, ks);
            for (int j = 15; j >= 8; j--) { if ((++ctr[j]) != 0) break; }
            size_t n = (chunk - i < 16) ? (chunk - i) : 16;
            for (size_t j = 0; j < n; j++) buf[i + j] ^= ks[j];
        }

        if (fwrite(buf, 1, chunk, of) != chunk) { perror("write"); return 1; }
        pos += chunk;
    }

    printf("done: total=0x%llx data=0x%llx zeros=0x%llx\n",
           (unsigned long long)total, (unsigned long long)dataBytes, (unsigned long long)zeroBytes);
    mbedtls_aes_free(&aes);
    fclose(of);
    fclose(nf);
    free(buf);
    free(regs);
    free(tbl);
    return 0;
}
