/* Tiny crypto helper built against the mbedtls bundled with hactool.
 *
 * usage:
 *   crypt_helper ecbdec  <key-hex> <data-hex>            -> prints hex
 *   crypt_helper ctrdec  <key-hex> <offset-dec> <file> <len-hex|dec>
 *
 * ctrdec decrypts <len> bytes at <offset> of <file> with AES-128-CTR using the
 * NCA counter convention: IV = 8 zero bytes || BE64(offset / 0x10).
 * Used to probe NCA section layouts without printing any key material.
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

static void put_be64(unsigned char *p, unsigned long long v) {
    for (int i = 7; i >= 0; i--) { p[i] = (unsigned char)(v & 0xFF); v >>= 8; }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s ecbdec <key> <data-hex> | ctrdec <key> <offset> <file> <len>\n", argv[0]);
        return 2;
    }
    unsigned char key[32];
    int klen = unhex(argv[2], key, sizeof(key));
    if (klen != 16 && klen != 32) {
        fprintf(stderr, "bad key length\n");
        return 2;
    }

    if ((strcmp(argv[1], "ecbdec") == 0 || strcmp(argv[1], "ecbenc") == 0) && argc == 4) {
        unsigned char in[256], out[256];
        int dlen = unhex(argv[3], in, sizeof(in));
        if (dlen <= 0 || dlen % 16) { fprintf(stderr, "bad data\n"); return 2; }
        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_dec(&ctx, key, (unsigned int)klen * 8);
        int encmode = (strcmp(argv[1], "ecbenc") == 0);
        if (encmode) {
            mbedtls_aes_setkey_enc(&ctx, key, (unsigned int)klen * 8);
        }
        for (int off = 0; off < dlen; off += 16)
            mbedtls_aes_crypt_ecb(&ctx, encmode ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, in + off, out + off);
        mbedtls_aes_free(&ctx);
        for (int i = 0; i < dlen; i++) printf("%02x", out[i]);
        printf("\n");
        return 0;
    }

    if (strcmp(argv[1], "ctrdec") == 0 && argc == 6) {
        unsigned long long offset = strtoull(argv[3], NULL, 0);
        unsigned long long len = strtoull(argv[5], NULL, 0);
        FILE *f = fopen(argv[4], "rb");
        if (!f) { perror("open"); return 1; }
        unsigned char *buf = malloc(len);
        if (!buf) { fprintf(stderr, "oom\n"); return 1; }
        fseeko(f, (off_t)offset, SEEK_SET);
        if (fread(buf, 1, len, f) != len) { fprintf(stderr, "short read\n"); return 1; }
        fclose(f);

        unsigned char iv[16] = {0}, ctr[16], stream[16];
        put_be64(iv + 8, offset / 0x10);
        memcpy(ctr, iv, 16);
        mbedtls_aes_context ctx;
        mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_enc(&ctx, key, (unsigned int)klen * 8);
        for (unsigned long long i = 0; i < len; i += 16) {
            mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, ctr, stream);
            /* big-endian increment of the low 8 bytes (offset/0x10) */
            for (int j = 15; j >= 8; j--) { if (++ctr[j] != 0) break; }
            unsigned long long n = (len - i < 16) ? (len - i) : 16;
            for (unsigned long long j = 0; j < n; j++) buf[i + j] ^= stream[j];
        }
        mbedtls_aes_free(&ctx);
        for (unsigned long long i = 0; i < len; i++) printf("%02x", buf[i]);
        printf("\n");
        free(buf);
        return 0;
    }

    if (strcmp(argv[1], "ctrfile") == 0 && (argc == 8 || argc == 9)) {
        /* ctrfile <key> <file> <file_off> <nonce_off> <len> <outfile> */
        unsigned long long foff = strtoull(argv[4], NULL, 0);
        unsigned long long noff = strtoull(argv[5], NULL, 0);
        unsigned long long len = strtoull(argv[6], NULL, 0);
        FILE *f = fopen(argv[3], "rb");
        if (!f) { perror("open"); return 1; }
        unsigned char *buf = malloc(len);
        if (!buf) { fprintf(stderr, "oom\n"); return 1; }
        fseeko(f, (off_t)foff, SEEK_SET);
        if (fread(buf, 1, len, f) != len) { fprintf(stderr, "short read\n"); return 1; }
        fclose(f);
        unsigned char ctr[16], stream[16];
        memset(ctr, 0, 16);
        if (argc == 9) {
            unsigned char up[8];
            if (unhex(argv[8], up, sizeof(up)) != 8) { fprintf(stderr, "bad upper iv\n"); return 2; }
            memcpy(ctr, up, 8);
        }
        unsigned long long blk = noff / 0x10;
        for (int i = 7; i >= 0; i--) { ctr[8 + i] = (unsigned char)(blk & 0xFF); blk >>= 8; }
        mbedtls_aes_context c;
        mbedtls_aes_init(&c);
        mbedtls_aes_setkey_enc(&c, key, (unsigned int)klen * 8);
        for (unsigned long long i = 0; i < len; i += 16) {
            mbedtls_aes_crypt_ecb(&c, MBEDTLS_AES_ENCRYPT, ctr, stream);
            for (int j = 15; j >= 8; j--) { if (++ctr[j] != 0) break; }
            unsigned long long n = (len - i < 16) ? (len - i) : 16;
            for (unsigned long long j = 0; j < n; j++) buf[i + j] ^= stream[j];
        }
        mbedtls_aes_free(&c);
        FILE *o = fopen(argv[7], "wb");
        if (!o) { perror("open out"); return 1; }
        fwrite(buf, 1, len, o);
        fclose(o);
        free(buf);
        printf("wrote %llu bytes\n", len);
        return 0;
    }

    fprintf(stderr, "bad mode\n");
    return 2;
}
