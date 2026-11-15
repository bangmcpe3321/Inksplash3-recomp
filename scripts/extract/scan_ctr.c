/* scan_ctr: find CTR-encrypted NCA section starts.
 *
 * For a given AES-128 content key, walk step-aligned offsets of a file,
 * decrypt 8 bytes with the NCA CTR convention (IV = 8*0x00 || BE64(off/0x10))
 * and report offsets whose plaintext starts with a known magic.
 * Prints only offsets, never key material.
 *
 * usage: scan_ctr <key-hex> <file> [step]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mbedtls/aes.h"

#define CHUNK (32u * 1024u * 1024u)

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <key-hex> <file> [step]\n", argv[0]);
        return 2;
    }
    unsigned char key[16];
    if (strlen(argv[1]) != 32) { fprintf(stderr, "bad key\n"); return 2; }
    for (int i = 0; i < 16; i++) {
        int hi = hexval(argv[1][2 * i]), lo = hexval(argv[1][2 * i + 1]);
        if (hi < 0 || lo < 0) { fprintf(stderr, "bad key\n"); return 2; }
        key[i] = (unsigned char)((hi << 4) | lo);
    }
    unsigned long long step = (argc > 3) ? strtoull(argv[3], NULL, 0) : 0x200;
    if (!step) step = 0x200;
    /* Optional 4th arg: fixed virtual offset for the CTR nonce (i.e. the
     * section's virtual start) instead of using the file offset itself. */
    int have_fixed = (argc > 4) && strcmp(argv[4], "off") != 0;
    unsigned long long fixed = have_fixed ? strtoull(argv[4], NULL, 0) : 0;
    unsigned long long nonce_base = have_fixed ? fixed : 0;
    /* Optional 5th arg: upper IV as 16 hex chars (big-endian bytes). */
    unsigned char upper[8] = {0};
    if (argc > 5) {
        if (strlen(argv[5]) != 16) { fprintf(stderr, "bad upper\n"); return 2; }
        for (int i = 0; i < 8; i++) {
            int hi = hexval(argv[5][2 * i]), lo = hexval(argv[5][2 * i + 1]);
            if (hi < 0 || lo < 0) { fprintf(stderr, "bad upper\n"); return 2; }
            upper[i] = (unsigned char)((hi << 4) | lo);
        }
    }

    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror("open"); return 1; }
    fseeko(f, 0, SEEK_END);
    unsigned long long size = (unsigned long long)ftello(f);
    fseeko(f, 0, SEEK_SET);

    unsigned char *buf = malloc(CHUNK);
    if (!buf) { fprintf(stderr, "oom\n"); return 1; }

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);

    unsigned char ks[16], ctr[16];
    long hits = 0;
    unsigned long long base = 0;
    while (base < size && hits <= 40) {
        size_t want = (size - base < CHUNK) ? (size_t)(size - base) : CHUNK;
        size_t got = fread(buf, 1, want, f);
        if (!got) break;
        for (unsigned long long off = base; off + 8 <= base + got; off += step) {
            const unsigned char *in = buf + (off - base);
            memcpy(ctr, upper, 8);
            memset(ctr + 8, 0, 8);
            unsigned long long blk = (have_fixed ? nonce_base : off) >> 4;
            for (int i = 7; i >= 0; i--) { ctr[8 + i] = (unsigned char)(blk & 0xFF); blk >>= 8; }
            mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, ctr, ks);
            unsigned char pt[8];
            for (int i = 0; i < 8; i++) pt[i] = in[i] ^ ks[i];
            if (!memcmp(pt, "PFS0", 4) || !memcmp(pt, "IVFC", 4) ||
                !memcmp(pt, "NSO0", 4) || !memcmp(pt, "NCA3", 4)) {
                printf("hit off=0x%llx magic=%.4s\n", off, pt);
                if (++hits > 40) break;
            }
        }
        base += got;
    }
    mbedtls_aes_free(&ctx);
    free(buf);
    if (!hits) printf("no hits\n");
    return 0;
}
