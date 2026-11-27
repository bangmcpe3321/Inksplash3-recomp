#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shader.h"

static int translate_word(uint64_t word, const char *path, const char *expected) {
    uint64_t code[6] = {0};
    code[1] = word;
    code[2] = 0xe300000000070000ull;  // EXIT @PT
    code[3] = 0xe300000000070000ull;

    ShaderKey key = {0};
    key.glsl = 1;
    ShaderInfo *info = maxwell_translate((const uint8_t *)code, sizeof code, STAGE_COMPUTE, &key);
    if (!info || !info->msl) {
        fprintf(stderr, "translation failed for %016llx: %s\n", (unsigned long long)word,
                info ? info->error : "no result");
        maxwell_free(info);
        return 1;
    }
    if (!strstr(info->msl, expected)) {
        fprintf(stderr, "translation for %016llx lacks expected expression: %s\n",
                (unsigned long long)word, expected);
        fprintf(stderr, "%s\n", info->msl);
        maxwell_free(info);
        return 1;
    }
    FILE *out = fopen(path, "wb");
    if (!out) {
        perror(path);
        maxwell_free(info);
        return 1;
    }
    size_t size = strlen(info->msl);
    int failed = fwrite(info->msl, 1, size, out) != size;
    if (fclose(out) != 0) failed = 1;
    maxwell_free(info);
    return failed;
}

static int rejects_word(uint64_t word, const char *expected_error) {
    uint64_t code[6] = {0};
    code[1] = word;
    code[2] = 0xe300000000070000ull;
    code[3] = 0xe300000000070000ull;
    ShaderKey key = {0};
    key.glsl = 1;
    ShaderInfo *info = maxwell_translate((const uint8_t *)code, sizeof code, STAGE_COMPUTE, &key);
    int ok = info && !info->msl && strstr(info->error, expected_error);
    if (!ok) fprintf(stderr, "unsupported LEA modifier was not rejected: %016llx (%s)\n",
                     (unsigned long long)word, info ? info->error : "no result");
    maxwell_free(info);
    return !ok;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    char path[1024];
    const uint64_t words[] = {
        0x5bdf7f81f2672525ull,
        0x5bdf7f81f2672727ull,
        0x5bdf7f81f2872722ull,
        0x5bd8000000000000ull | (7ull << 48) | (7ull << 39) | (20ull << 28) |
            (1ull << 37) | (9ull << 20) | (6ull << 8) | (7ull << 16) | 5ull,
    };
    const char *expressions[] = {
        "r38 + uint(((uint64_t(0u) << 32 | uint64_t(r37))) >> 1u)",
        "r38 + uint(((uint64_t(0u) << 32 | uint64_t(r39))) >> 1u)",
        "r40 + uint(((uint64_t(0u) << 32 | uint64_t(r39))) >> 1u)",
        "r9 + uint((0ul - (uint64_t(r7) << 32 | uint64_t(r6))) >> 12u)",
    };
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++) {
        snprintf(path, sizeof path, "%s/lea_hi_%u.comp", argv[1], (unsigned)i);
        if (translate_word(words[i], path, expressions[i])) return 1;
    }
    if (rejects_word(words[0] | (1ull << 38), "lea hi reg with x/cc/pred")) return 1;
    if (rejects_word(words[0] | (1ull << 47), "lea hi reg with x/cc/pred")) return 1;
    if (rejects_word((words[0] & ~(7ull << 48)) | (6ull << 48), "lea hi reg with x/cc/pred")) return 1;
    puts("Maxwell LEA.HI tests passed");
    return 0;
}
