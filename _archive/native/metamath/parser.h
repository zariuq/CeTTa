#ifndef CETTA_NATIVE_METAMATH_PARSER_H
#define CETTA_NATIVE_METAMATH_PARSER_H

#include "atom.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct CettaMetamathParser CettaMetamathParser;

typedef enum {
    CETTA_MM_NEXT_STMT = 0,
    CETTA_MM_NEXT_EOF = 1,
    CETTA_MM_NEXT_ERROR = 2
} CettaMetamathNextStatus;

CettaMetamathParser *cetta_metamath_parser_open(const char *path,
                                                bool pverify_shape,
                                                bool dag_format,
                                                char *errbuf,
                                                size_t errbuf_sz);
void cetta_metamath_parser_close(CettaMetamathParser *parser);

CettaMetamathNextStatus cetta_metamath_parser_next_stmt(CettaMetamathParser *parser,
                                                        Arena *a,
                                                        Atom **stmt_out,
                                                        char *errbuf,
                                                        size_t errbuf_sz);

Atom *cetta_metamath_collect_file(Arena *a,
                                  const char *path,
                                  bool pverify_shape,
                                  bool dag_format,
                                  char *errbuf,
                                  size_t errbuf_sz);

Atom *cetta_metamath_read_tokens_file(Arena *a,
                                      const char *path,
                                      char *errbuf,
                                      size_t errbuf_sz);

#endif /* CETTA_NATIVE_METAMATH_PARSER_H */
