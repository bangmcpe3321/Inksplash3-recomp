/* find_nonce.c - given a known plaintext at a known file offset, search for the
 * AES-CTR counter block that explains it.
 *
 * usage: find_nonce <key-hex> <file> <offset> <plaintext-hex(<=32 bytes)> [upper-hex16] [max-search-bits]
 *
 * Tries counter blocks BE64(upper)||BE64(x) for x in [0, 2^max-bits).
 * Prints the matching x (counts as IV nonce), never key material.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/aes.h"

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

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <key> <file> <offset> <plaintext-hex> [upper] [bits]\n", argv[0]);
        return 2;
    }
    unsigned char key[32], pt[64], ct[64];
    int klen = unhex(argv[1], key, sizeof(key));
    unsigned long long off = strtoull(argv[3], NULL, 0);
    int plen = unhex(argv[4], pt, sizeof(pt));
    if (klen != 16 || plen <= 0) { fprintf(stderr, "bad args\n"); return 2; }

    unsigned char upper[8] = {0};
    if (argc > 5 && strcmp(argv[5], "-") != 0) {
        if (unhex(argv[5], upper, 8) != 8) { fprintf(stderr, "bad upper\n"); return 2; }
    }
    int bits = (argc > 6) ? atoi(argv[6]) : 24;

    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror("open"); return 1; }
    fseeko(f, (off_t)off, SEEK_SET);
    if (fread(ct, 1, (size_t)plen, f) != (size_t)plen) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);

    unsigned char need[64];
    for (int i = 0; i < plen; i++) need[i] = ct[i] ^ pt[i];

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, (unsigned int)klen * 8);

    unsigned char ctr[16], ks[16];
    memcpy(ctr, upper, 8);
    unsigned long long limit = 1ULL << bits;
    int found = 0;
    for (unsigned long long x = 0; x < limit; x++) {
        unsigned long long v = x;
        for (int i = 7; i >= 0; i--) { ctr[8 + i] = (unsigned char)(v & 0xFF); v >>= 8; }
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, ctr, ks);
        if (memcmp(ks, need, (size_t)plen) == 0) {
            printf("nonce_block=0x%llx (byte offset 0x%llx)\n", x, x * 16);
            if (++found >= 3) break;
        }
    }
    mbedtls_aes_free(&aes);
    if (!found) printf("no match in 2^%d tries\n", bits);
    return 0;
}
