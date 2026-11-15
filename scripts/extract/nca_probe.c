/* nca_probe.c - validate NCA section decryption against the section master hash.
 *
 * usage: nca_probe <keys.txt> <nca> <decrypted_header.bin> <kaek-index-hex|t> <section-index> [upper-override-hex16]
 *
 * If the 4th argument is "t", the 4th key index is not used and instead the
 * content key is taken from the 5th argument... (not used here).
 *
 * Prints, for each candidate content key (decrypted from the NCA key area with
 * the given key_area_key_application index), whether decrypting the section's
 * master-hash region yields the expected SHA-256. No key bytes are printed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"

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

/* Look up "name = hex" in the keys file. Returns hex length or -1. */
static int find_key(const char *path, const char *name, unsigned char *out, size_t max) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[4096];
    size_t namelen = strlen(name);
    int found = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, name, namelen) != 0) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        char *p = eq + 1;
        while (*p == ' ' || *p == '\t') p++;
        char *end = p;
        while (*end && *end != '\n' && *end != '\r' && *end != ' ') end++;
        *end = 0;
        found = unhex(p, out, max);
        break;
    }
    fclose(f);
    return found;
}

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const unsigned char *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <keys> <nca> <hdr.bin> <kaek-idx-hex> <section> [upper-hex16]\n", argv[0]);
        return 2;
    }
    const char *keysPath = argv[1], *ncaPath = argv[2], *hdrPath = argv[3];
    int explicit_key = (strncmp(argv[4], "key:", 4) == 0);
    unsigned char kaek[16];
    if (explicit_key) {
        if (unhex(argv[4] + 4, kaek, sizeof(kaek)) != 16) { fprintf(stderr, "bad key\n"); return 2; }
    } else {
        unsigned kaek_idx = (unsigned)strtoul(argv[4], NULL, 16);
        char name[64];
        snprintf(name, sizeof(name), "key_area_key_application_%02x", kaek_idx);
        if (find_key(keysPath, name, kaek, sizeof(kaek)) != 16) {
            fprintf(stderr, "missing %s\n", name);
            return 1;
        }
    }
    int sect = atoi(argv[5]);

    unsigned char hdr[0xC00];
    FILE *hf = fopen(hdrPath, "rb");
    if (!hf) { perror("hdr"); return 1; }
    if (fread(hdr, 1, 0xC00, hf) != 0xC00) { fprintf(stderr, "short hdr\n"); return 1; }
    fclose(hf);

    unsigned char *fs = hdr + 0x400 + sect * 0x200;
    unsigned char *sb = fs + 8;
    uint32_t gen = rd32(fs + 0x140), secval = rd32(fs + 0x144);
    uint64_t upper = (uint64_t)gen | ((uint64_t)secval << 32);
    if (argc > 6) {
        unsigned char up[8];
        if (unhex(argv[6], up, 8) != 8) { fprintf(stderr, "bad upper\n"); return 2; }
        upper = 0;
        for (int i = 0; i < 8; i++) upper = (upper << 8) | up[i];
    }

    unsigned char master[0x20];
    uint64_t hashOff = 0, hashSize = 0;
    int isRomfs = (memcmp(sb, "IVFC", 4) == 0);
    if (isRomfs) {
        memcpy(master, sb + 0xC0, 0x20);
        hashOff = rd64(sb + 0x10);
        hashSize = rd64(sb + 0x18);
    } else {
        memcpy(master, sb, 0x20);
        hashOff = rd64(sb + 0x28);
        hashSize = rd64(sb + 0x30);
    }

    uint64_t sectionStart = (uint64_t)rd32(hdr + 0x240 + sect * 0x10) * 0x200;

    printf("section %d: start=0x%llx hash_region off=0x%llx size=0x%llx gen=%u secure=%u upper=0x%016llx romfs=%d\n",
           sect, (unsigned long long)sectionStart, (unsigned long long)hashOff,
           (unsigned long long)hashSize, gen, secval, (unsigned long long)upper, isRomfs);

    FILE *f = fopen(ncaPath, "rb");
    if (!f) { perror("nca"); return 1; }

    unsigned char *buf = malloc(hashSize ? hashSize : 1);
    if (!buf) { fprintf(stderr, "oom\n"); return 1; }

    /* Decrypt each of the 4 key area entries and test it. */
    for (int kidx = 0; kidx < (explicit_key ? 1 : 4); kidx++) {
        unsigned char enc[16], ckey[16];
        memcpy(ckey, kaek, 16);
        if (!explicit_key) {
            memcpy(enc, hdr + 0x300 + kidx * 16, 16);
            mbedtls_aes_context aesd;
            mbedtls_aes_init(&aesd);
            mbedtls_aes_setkey_dec(&aesd, kaek, 128);
            mbedtls_aes_crypt_ecb(&aesd, MBEDTLS_AES_DECRYPT, enc, ckey);
            mbedtls_aes_free(&aesd);

            int allzero = 1;
            for (int i = 0; i < 16; i++) if (ckey[i]) allzero = 0;
            if (allzero) { printf("  key %d: zero key, skipped\n", kidx); continue; }
        }

        /* Read the master-hash-covered region. */
        fseeko(f, (off_t)(sectionStart + hashOff), SEEK_SET);
        if (fread(buf, 1, hashSize, f) != hashSize) { printf("  key %d: read fail\n", kidx); continue; }

        /* CTR decrypt: iv = BE64(upper) || BE64((sectionStart+hashOff)/0x10) */
        unsigned char iv[16], ctr[16], ks[16];
        for (int i = 0; i < 8; i++) iv[i] = (upper >> (56 - 8 * i)) & 0xFF;
        uint64_t blk = (sectionStart + hashOff) / 0x10;
        for (int i = 0; i < 8; i++) iv[8 + i] = (blk >> (56 - 8 * i)) & 0xFF;
        memcpy(ctr, iv, 16);
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        mbedtls_aes_setkey_enc(&aes, ckey, 128);
        for (size_t i = 0; i < hashSize; i += 16) {
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, ctr, ks);
            for (int j = 15; j >= 8; j--) { if ((++ctr[j]) != 0) break; }
            size_t n = (hashSize - i < 16) ? (hashSize - i) : 16;
            for (size_t j = 0; j < n; j++) buf[i + j] ^= ks[j];
        }
        mbedtls_aes_free(&aes);

        unsigned char dig[32];
        mbedtls_sha256(buf, hashSize, dig, 0);
        printf("  key %d: %s\n", kidx, memcmp(dig, master, 0x20) == 0 ? "MATCH" : "no");
    }

    fclose(f);
    free(buf);
    return 0;
}
