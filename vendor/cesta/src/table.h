#ifndef CESTA_TABLE_H
#define CESTA_TABLE_H

#include "cesta.h"

typedef enum { CE_COLUMN_NAT, CE_COLUMN_IDENTIFIER, CE_COLUMN_TEXT, CE_COLUMN_BOOL,
               CE_COLUMN_ENUM, CE_COLUMN_REF } CeColumnKind;
typedef struct {
    CeText name;
    CeColumnKind kind;
    CeText *choices;
    size_t nchoices;
    CeText target, target_column;
} CeColumn;
typedef struct { size_t *columns, len; } CeTableKey;
typedef struct {
    CeText name;
    size_t offset;
    CeColumn *columns;
    size_t ncolumns;
    CeTableKey key, *unique;
    size_t nunique, dense;
    CeAtom **rows;
    size_t nrows;
} CeTable;
typedef struct {
    CeArena *arena;
    CeTable *tables;
    size_t len;
    const CeAtom *source;
} CeTables;

/* Storage belongs to arena. Validation never changes row order or supplied IDs. */
bool ce_tables_read(CeArena *arena, const CeAtom *data, CeTables *out, CeError *error);
bool ce_tables_load(CeArena *arena, const char *path, CeTables *out, CeError *error);
bool ce_tables_validate(const CeTables *tables, CeError *error);
const CeTable *ce_tables_find(const CeTables *tables, CeText name);
size_t ce_table_column(const CeTable *table, CeText name);
bool ce_table_nat(const CeAtom *atom, uint64_t *out);
bool ce_table_c_name(CeText name);
bool ce_table_cell_type(const CeTables *tables, const CeColumn *column,
                        const CeAtom *cell, CeError *error);
void ce_tables_write(const CeTables *tables, FILE *file);
/* Ordered projection: selected columns, in the requested order, for every row. */
void ce_table_rows(const CeTable *table, const size_t *columns, size_t len, FILE *file);
void ce_table_c_cell(const CeAtom *cell, const CeColumn *column, FILE *file);
bool ce_table_xmacro(const CeTables *tables, const CeTable *table, CeText name, const size_t *columns, size_t len,
                     FILE *file, CeError *error);
int ce_table_command(int argc, char **argv);

#endif
