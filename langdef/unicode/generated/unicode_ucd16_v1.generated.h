/* Generated from unicode_codepoint_classes16_v1.metta.
 * Unicode-derived data are covered by ../LICENSE.txt (Unicode-3.0). */
#ifndef CETTA_UNICODE_UCD16_DATA_V1_H
#define CETTA_UNICODE_UCD16_DATA_V1_H
#include "../../../native/unicode_class_v1.h"
typedef struct { const char *name; const char *canonical; } UnicodeV1PropertyAlias;
typedef struct { const char *property; const char *name; const char *canonical; } UnicodeV1ValueAlias;
typedef struct { const char *property; const char *value; uint32_t begin; uint32_t len; } UnicodeV1PropertyRow;
typedef struct { uint32_t scalar; uint32_t begin; uint32_t len; } UnicodeV1FoldRow;
typedef struct { const char *name; UnicodeV1Ranges ranges; } UnicodeV1LexicalRow;
typedef struct { const char *name; UnicodeV1DigitClass digits; } UnicodeV1DigitRow;
extern const CettaLpNativeUnicodeRange unicode_v1_ucd_ranges[];
extern const UnicodeV1PropertyRow unicode_v1_ucd_properties[];
extern const size_t unicode_v1_ucd_property_len;
extern const UnicodeV1PropertyAlias unicode_v1_ucd_property_aliases[];
extern const size_t unicode_v1_ucd_property_alias_len;
extern const UnicodeV1ValueAlias unicode_v1_ucd_value_aliases[];
extern const size_t unicode_v1_ucd_value_alias_len;
extern const uint32_t unicode_v1_ucd_decimal_zeroes[];
extern const size_t unicode_v1_ucd_decimal_len;
extern const UnicodeV1FoldRow unicode_v1_ucd_fold_index[];
extern const size_t unicode_v1_ucd_fold_index_len;
extern const uint32_t unicode_v1_ucd_fold_members[];
extern const UnicodeV1Ranges unicode_v1_ucd_domains[5];
extern const char unicode_v1_ucd_version[];
extern const UnicodeV1LexicalRow unicode_v1_ucd_lexical_classes[];
extern const size_t unicode_v1_ucd_lexical_class_len;
extern const UnicodeV1DigitRow unicode_v1_ucd_digit_classes[];
extern const size_t unicode_v1_ucd_digit_class_len;
#endif
