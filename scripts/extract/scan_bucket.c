/* scan_bucket.c - locate an NCA sparse/compression bucket-tree node header.
 *
 * usage: scan_bucket <key-hex> <file> <upper-hex16> <target-total-hex> <mode>
 *   mode 0: CTR nonce = file offset / 0x10 (table CTR base == table offset)
 *   mode 1: CTR nonce = fixed value passed as 6th arg
 *
 * Prints offsets whose decrypted 16 bytes look like
 *   { u32 pad(0), u32 num_buckets(1..32), u64 total_size == target }.
 * No key material is printed.
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

static int parse_hex(const char *s, unsigned char *out, size_t n) {
    if (strlen(s) != 2 * n) return -1;
    for (size_t i = 0; i < n; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <key> <file> <upper> <target-total> <mode> [fixed-nonce]\n", argv[0]);
        return 2;
    }
    unsigned char key[16], upper[8];
    if (parse_hex(argv[1], key, 16) || parse_hex(argv[3], upper, 8)) {
        fprintf(stderr, "bad key/upper\n");
        return 2;
    }
    unsigned long long target = strtoull(argv[4], NULL, 0);
    int mode = atoi(argv[5]);
    unsigned long long fixed = (argc > 6) ? strtoull(argv[6], NULL, 0) : 0;

    FILE *f = fopen(argv[2], "rb");
    if (!f) { perror("open"); return 1; }
    fseeko(f, 0, SEEK_END);
    unsigned long long size = (unsigned long long)ftello(f);
    fseeko(f, 0, SEEK_SET);

    unsigned char *buf = malloc(CHUNK);
    if (!buf) { fprintf(stderr, "oom\n"); return 1; }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);

    unsigned char ctr[16], ks[16], pt[16];
    long hits = 0;
    unsigned long long base = 0;
    while (base < size) {
        size_t want = (size - base < CHUNK) ? (size_t)(size - base) : CHUNK;
        size_t got = fread(buf, 1, want, f);
        if (!got) break;
        for (unsigned long long off = base; off + 16 <= base + got; off += 0x200) {
            unsigned long long blk = (mode == 0) ? (off >> 4) : (fixed >> 4);
            memcpy(ctr, upper, 8);
            for (int i = 7; i >= 0; i--) { ctr[8 + i] = (unsigned char)(blk & 0xFF); blk >>= 8; }
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, ctr, ks);
            const unsigned char *in = buf + (off - base);
            for (int i = 0; i < 16; i++) pt[i] = in[i] ^ ks[i];
            unsigned pad, nb;
            unsigned long long tot;
            memcpy(&pad, pt, 4);
            memcpy(&nb, pt + 4, 4);
            memcpy(&tot, pt + 8, 8);
            if (pad == 0 && nb >= 1 && nb <= 32 && tot == target) {
                printf("hit off=0x%llx num_buckets=%u total=0x%llx\n", off, nb, tot);
                if (++hits > 20) { free(buf); return 0; }
            }
        }
        base += got;
    }
    free(buf);
    if (!hits) printf("no hits\n");
    return 0;
}
