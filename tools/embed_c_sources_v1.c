#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int identifier_valid(const char *name) {
    const unsigned char *cursor = (const unsigned char *)name;
    if (!cursor || !(isalpha(*cursor) || *cursor == '_')) return 0;
    for (cursor++; *cursor; cursor++) {
        if (!(isalnum(*cursor) || *cursor == '_')) return 0;
    }
    return 1;
}

static uint8_t *read_file(const char *path, size_t *len_out) {
    FILE *file = fopen(path, "rb");
    long length;
    uint8_t *bytes;
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)length ? (size_t)length : 1u);
    if (!bytes || fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *len_out = (size_t)length;
    return bytes;
}

static int emit_array(FILE *output, const char *name,
                      const uint8_t *bytes, size_t len) {
    size_t index;
    if (fprintf(output, "const uint8_t %s[] = {", name) < 0) return 0;
    for (index = 0u; index < len; index++) {
        if (index % 12u == 0u && fputs("\n    ", output) == EOF) return 0;
        if (fprintf(output, "0x%02x%s", (unsigned int)bytes[index],
                    index + 1u == len ? "" : ", ") < 0) return 0;
    }
    if (len > 0u && fputc('\n', output) == EOF) return 0;
    return fprintf(output,
                   "};\nconst size_t %s_len = sizeof(%s);\n\n",
                   name, name) >= 0;
}

int main(int argc, char **argv) {
    FILE *output;
    int index;
    if (argc < 4 || (argc - 2) % 2 != 0) {
        fprintf(stderr,
                "usage: %s OUTPUT_C SYMBOL SOURCE [SYMBOL SOURCE ...]\n",
                argc > 0 ? argv[0] : "embed_c_sources_v1");
        return 2;
    }
    output = fopen(argv[1], "wb");
    if (!output) {
        fprintf(stderr, "cannot open %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    if (fputs("#include <stddef.h>\n#include <stdint.h>\n\n", output) == EOF) {
        fclose(output);
        return 1;
    }
    for (index = 2; index < argc; index += 2) {
        uint8_t *bytes;
        size_t len = 0u;
        if (!identifier_valid(argv[index])) {
            fprintf(stderr, "invalid C identifier: %s\n", argv[index]);
            fclose(output);
            return 2;
        }
        bytes = read_file(argv[index + 1], &len);
        if (!bytes) {
            fprintf(stderr, "cannot read %s\n", argv[index + 1]);
            fclose(output);
            return 1;
        }
        if (!emit_array(output, argv[index], bytes, len)) {
            free(bytes);
            fclose(output);
            return 1;
        }
        free(bytes);
    }
    if (fclose(output) != 0) return 1;
    return 0;
}
