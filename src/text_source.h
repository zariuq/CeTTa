#ifndef CETTA_TEXT_SOURCE_H
#define CETTA_TEXT_SOURCE_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CETTA_TEXT_SOURCE_BUFFER_SIZE 65536

typedef enum {
    CETTA_TEXT_READ_OK, CETTA_TEXT_READ_IO, CETTA_TEXT_READ_MEMORY,
    CETTA_TEXT_READ_OVERFLOW
} CettaTextReadStatus;
/* Read owned bytes with an extra NUL terminator. Length still includes any
 * embedded NUL; source consumers decide whether those bytes are legal. */
CettaTextReadStatus cetta_text_read_file(const char *path, uint8_t **bytes,
    size_t *length, int *system_error);

typedef struct {
    char path[PATH_MAX];
    char dir[PATH_MAX];
    FILE *fp;
    unsigned char buf[CETTA_TEXT_SOURCE_BUFFER_SIZE];
    size_t pos;
    size_t len;
    bool eof;
    uint32_t line;
    uint32_t col;
} CettaTextSource;

bool cetta_text_path_parent_dir(char *dst, size_t dst_sz, const char *path);
bool cetta_text_path_resolve(const char *base_dir, const char *path,
                             char *resolved, size_t resolved_sz);

bool cetta_text_source_open(CettaTextSource *src, const char *base_dir,
                            const char *path, char *errbuf, size_t errbuf_sz);
void cetta_text_source_close(CettaTextSource *src);

bool cetta_text_source_peek(CettaTextSource *src, size_t offset, int *ch_out,
                            char *errbuf, size_t errbuf_sz);
void cetta_text_source_advance(CettaTextSource *src, size_t count);

#endif /* CETTA_TEXT_SOURCE_H */
