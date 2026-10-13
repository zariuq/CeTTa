#include "native/parser_baked_v1.h"

#include <string.h>

#if !defined(CETTA_NO_STDLIB) && !defined(CETTA_PARSER_COMPILER)
#include CETTA_PARSER_BAKED_HEADER
#endif

const CettaBakedParserV1 *cetta_parser_baked_v1_find(const char *name) {
#if !defined(CETTA_NO_STDLIB) && !defined(CETTA_PARSER_COMPILER)
    if (name)
        for (size_t i = 0u; i < CETTA_BAKED_PARSER_COUNT_V1; ++i)
            if (!strcmp(name, cetta_baked_parsers_v1[i].name))
                return &cetta_baked_parsers_v1[i];
#else
    (void)name;
#endif
    return NULL;
}
