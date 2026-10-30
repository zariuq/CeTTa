#define _POSIX_C_SOURCE 200809L
#include "table.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool form(const CeAtom *a, const char *name) {
    return a->kind == CE_ATOM_LIST && a->len && a->items[0]->kind == CE_ATOM_SYMBOL && ce_is(a->items[0]->text,name);
}
static bool identifier(CeText text) {
    if (!text.len) return false;
    for (size_t i = 0; i < text.len; ++i) {
        unsigned c = text.data[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (i && c >= '0' && c <= '9'))) return false;
    }
    return true;
}
/* Logical Identifier cells include keyword spellings; declarations do not. */
bool ce_table_c_name(CeText name) {
    static const char *const keywords[] = {
        "auto","break","case","char","const","continue","default","do","double","else","enum","extern",
        "float","for","goto","if","inline","int","long","register","restrict","return","short","signed",
        "sizeof","static","struct","switch","typedef","union","unsigned","void","volatile","while",
        "_Alignas","_Alignof","_Atomic","_Bool","_Complex","_Generic","_Imaginary","_Noreturn","_Static_assert",
        "_Thread_local","asm","typeof","__asm","__asm__","__attribute","__attribute__","__auto_type",
        "__typeof","__typeof__","__alignof","__alignof__","__const","__const__","__inline","__inline__",
        "__signed","__signed__","__volatile","__volatile__","__restrict","__restrict__","__complex__",
        "__extension__","__label__","__real","__real__","__imag","__imag__","__int128",
        "__builtin_va_arg","__builtin_offsetof","__builtin_types_compatible_p","__builtin_choose_expr"
    };
    if (!identifier(name)) return false;
    for (size_t i = 0; i < sizeof(keywords)/sizeof(*keywords); ++i)
        if (ce_is(name,keywords[i])) return false;
    return true;
}
static bool name_of(const CeAtom *atom, CeText *out, CeError *error) {
    if (atom->kind != CE_ATOM_SYMBOL || !identifier(atom->text))
        return ce_error(error,atom->offset,"table and column names require identifiers");
    *out = atom->text; return true;
}
bool ce_table_nat(const CeAtom *atom, uint64_t *out) {
    if (atom->kind != CE_ATOM_SYMBOL || !atom->text.len ||
        (atom->text.len > 1 && atom->text.data[0] == '0')) return false;
    uint64_t n = 0;
    for (size_t i = 0; i < atom->text.len; ++i) {
        unsigned c = atom->text.data[i];
        if (c < '0' || c > '9' || n > (UINT64_MAX-(c-'0'))/10) return false;
        n = n*10+(c-'0');
    }
    *out = n; return true;
}
const CeTable *ce_tables_find(const CeTables *tables, CeText name) {
    for (size_t i = 0; i < tables->len; ++i)
        if (ce_equal(tables->tables[i].name,name)) return &tables->tables[i];
    return NULL;
}
size_t ce_table_column(const CeTable *table, CeText name) {
    for (size_t i = 0; i < table->ncolumns; ++i)
        if (ce_equal(table->columns[i].name,name)) return i;
    return SIZE_MAX;
}
static bool column_type(CeArena *arena, const CeAtom *type, CeColumn *column, CeError *error) {
    if (type->kind == CE_ATOM_SYMBOL) {
        if (ce_is(type->text,"Nat")) column->kind = CE_COLUMN_NAT;
        else if (ce_is(type->text,"Identifier")) column->kind = CE_COLUMN_IDENTIFIER;
        else if (ce_is(type->text,"Text")) column->kind = CE_COLUMN_TEXT;
        else if (ce_is(type->text,"Bool")) column->kind = CE_COLUMN_BOOL;
        else return ce_error(error,type->offset,"unknown table column type");
    } else if (form(type,"Enum") && type->len > 1) {
        column->kind = CE_COLUMN_ENUM;
        column->nchoices = type->len-1;
        column->choices = ce_alloc(arena,column->nchoices*sizeof(*column->choices));
        for (size_t i = 0; i < column->nchoices; ++i) {
            if (!name_of(type->items[i+1],&column->choices[i],error)) return false;
            for (size_t j = 0; j < i; ++j) if (ce_equal(column->choices[i],column->choices[j]))
                return ce_error(error,type->items[i+1]->offset,"duplicate enum choice");
        }
    } else if (form(type,"Ref") && type->len == 3) {
        column->kind = CE_COLUMN_REF;
        if (!name_of(type->items[1],&column->target,error) ||
            !name_of(type->items[2],&column->target_column,error)) return false;
    } else return ce_error(error,type->offset,"column type is Nat, Identifier, Text, Bool, (Enum ...), or (Ref TABLE COLUMN)");
    return true;
}
static bool read_key(CeArena *arena, const CeTable *table, const CeAtom *data, CeTableKey *out, CeError *error) {
    if (data->len < 2) return ce_error(error,data->offset,"table key requires columns");
    out->len = data->len-1; out->columns = ce_alloc(arena,out->len*sizeof(*out->columns));
    for (size_t i = 0; i < out->len; ++i) {
        const CeAtom *a = data->items[i+1];
        out->columns[i] = a->kind == CE_ATOM_SYMBOL ? ce_table_column(table,a->text) : SIZE_MAX;
        if (out->columns[i] == SIZE_MAX) return ce_error(error,a->offset,"key names an unknown column");
        for (size_t j = 0; j < i; ++j) if (out->columns[j] == out->columns[i])
            return ce_error(error,a->offset,"key repeats a column");
    }
    return true;
}
static bool read_table(CeArena *arena, const CeAtom *data, CeTable *table, CeError *error) {
    table->offset = data->offset; table->dense = SIZE_MAX;
    if (!form(data,"Table") || data->len < 5 || !name_of(data->items[1],&table->name,error))
        return ce_error(error,data->offset,"table requires (Table NAME (Columns ...) (Key ...) ... (Rows ...))");
    const CeAtom *columns = data->items[2];
    if (!form(columns,"Columns") || columns->len < 2) return ce_error(error,columns->offset,"table requires nonempty Columns");
    table->ncolumns = columns->len-1;
    table->columns = ce_alloc(arena,table->ncolumns*sizeof(*table->columns));
    for (size_t i = 0; i < table->ncolumns; ++i) {
        const CeAtom *c = columns->items[i+1];
        if (!form(c,"Column") || c->len != 3 || !name_of(c->items[1],&table->columns[i].name,error) ||
            !column_type(arena,c->items[2],&table->columns[i],error))
            return error->message[0] ? false : ce_error(error,c->offset,"column requires (Column NAME TYPE)");
        for (size_t j = 0; j < i; ++j) if (ce_equal(table->columns[i].name,table->columns[j].name))
            return ce_error(error,c->offset,"column declared twice");
    }
    if (!form(data->items[3],"Key") || !read_key(arena,table,data->items[3],&table->key,error))
        return error->message[0] ? false : ce_error(error,data->offset,"table requires one primary Key after Columns");
    table->unique = ce_alloc(arena,data->len*sizeof(*table->unique));
    size_t pos = 4;
    while (pos < data->len && form(data->items[pos],"Unique")) {
        if (!read_key(arena,table,data->items[pos],&table->unique[table->nunique++],error)) return false;
        ++pos;
    }
    if (pos < data->len && form(data->items[pos],"Dense")) {
        const CeAtom *d = data->items[pos++];
        if (d->len != 2 || d->items[1]->kind != CE_ATOM_SYMBOL ||
            (table->dense = ce_table_column(table,d->items[1]->text)) == SIZE_MAX)
            return ce_error(error,d->offset,"Dense requires an existing column");
        if (table->key.len != 1 || table->key.columns[0] != table->dense ||
            table->columns[table->dense].kind != CE_COLUMN_NAT)
            return ce_error(error,d->offset,"Dense requires the singleton Nat primary key");
    }
    if (pos+1 != data->len || !form(data->items[pos],"Rows"))
        return ce_error(error,data->offset,"expected Rows after Key, Unique and optional Dense");
    const CeAtom *rows = data->items[pos]; table->nrows = rows->len-1;
    table->rows = ce_alloc(arena,(table->nrows+1)*sizeof(*table->rows));
    for (size_t i = 0; i < table->nrows; ++i) {
        const CeAtom *r = rows->items[i+1];
        if (!form(r,"Row") || r->len != table->ncolumns+1)
            return ce_error(error,r->offset,"row has the wrong number of cells");
        table->rows[i] = (CeAtom *)r;
    }
    return true;
}
static const CeColumn *scalar_column(const CeTables *tables, const CeColumn *column,
                                     const CeTable **target, CeError *error, size_t offset) {
    if (column->kind != CE_COLUMN_REF) return column;
    const CeTable *t = ce_tables_find(tables,column->target);
    size_t c = t ? ce_table_column(t,column->target_column) : SIZE_MAX;
    if (!t || c == SIZE_MAX || t->key.len != 1 || t->key.columns[0] != c ||
        t->columns[c].kind == CE_COLUMN_REF) {
        ce_error(error,offset,"reference must target a direct scalar singleton primary key"); return NULL;
    }
    if (target) *target = t;
    return &t->columns[c];
}
static bool cell_type(const CeAtom *a, const CeColumn *column) {
    uint64_t n;
    switch (column->kind) {
    case CE_COLUMN_NAT: return ce_table_nat(a,&n);
    case CE_COLUMN_TEXT: return a->kind == CE_ATOM_TEXT;
    case CE_COLUMN_BOOL: return a->kind == CE_ATOM_SYMBOL && (ce_is(a->text,"true") || ce_is(a->text,"false"));
    case CE_COLUMN_IDENTIFIER: return a->kind == CE_ATOM_SYMBOL && identifier(a->text);
    case CE_COLUMN_ENUM:
        for (size_t i = 0; a->kind == CE_ATOM_SYMBOL && i < column->nchoices; ++i)
            if (ce_equal(a->text,column->choices[i])) return true;
        return false;
    case CE_COLUMN_REF: return false;
    }
    return false;
}
static bool equal_cell(const CeAtom *a, const CeAtom *b) {
    return a->kind == b->kind && ce_equal(a->text,b->text);
}
bool ce_table_cell_type(const CeTables *tables, const CeColumn *column,
                        const CeAtom *cell, CeError *error) {
    const CeColumn *scalar = scalar_column(tables,column,NULL,error,cell->offset);
    if (!scalar) return false;
    return cell_type(cell,scalar) || ce_error(error,cell->offset,"filter value does not have its column type");
}
static bool equal_key(const CeAtom *a, const CeAtom *b, CeTableKey key) {
    for (size_t k = 0; k < key.len; ++k)
        if (!equal_cell(a->items[key.columns[k]+1],b->items[key.columns[k]+1])) return false;
    return true;
}
static bool unique_key(const CeTable *t, CeTableKey key, CeError *error) {
    for (size_t i = 0; i < t->nrows; ++i) for (size_t j = 0; j < i; ++j)
        if (equal_key(t->rows[i],t->rows[j],key)) {
            CeText n = t->columns[key.columns[0]].name;
            return ce_error(error,t->rows[i]->offset,key.len == 1 ? "duplicate row %.*s" : "duplicate composite key beginning with %.*s",
                            (int)n.len,n.data);
        }
    return true;
}
bool ce_tables_validate(const CeTables *tables, CeError *error) {
    for (size_t i = 0; i < tables->len; ++i) {
        const CeTable *t = &tables->tables[i];
        for (size_t j = 0; j < i; ++j) if (ce_equal(t->name,tables->tables[j].name))
            return ce_error(error,t->offset,"table declared twice");
        for (size_t c = 0; c < t->ncolumns; ++c) {
            const CeTable *target = NULL;
            const CeColumn *scalar = scalar_column(tables,&t->columns[c],&target,error,t->offset);
            if (!scalar) return false;
            size_t target_column = target ? target->key.columns[0] : 0;
            for (size_t r = 0; r < t->nrows; ++r) {
                const CeAtom *cell = t->rows[r]->items[c+1];
                if (!cell_type(cell,scalar))
                    return ce_error(error,cell->offset,scalar->kind == CE_COLUMN_NAT ?
                        "row requires a canonical uint64 decimal natural number" : "cell does not have its declared column type");
                if (target) {
                    bool found = false;
                    for (size_t k = 0; k < target->nrows && !found; ++k)
                        found = equal_cell(cell,target->rows[k]->items[target_column+1]);
                    if (!found) return ce_error(error,cell->offset,
                        "reference %.*s.%.*s value %.*s names no row in %.*s.%.*s",
                        (int)t->name.len,t->name.data,(int)t->columns[c].name.len,t->columns[c].name.data,
                        (int)cell->text.len,cell->text.data,(int)target->name.len,target->name.data,
                        (int)target->columns[target_column].name.len,target->columns[target_column].name.data);
                }
            }
        }
        if (t->dense != SIZE_MAX) for (size_t r = 0; r < t->nrows; ++r) {
            uint64_t n = 0;
            if (!ce_table_nat(t->rows[r]->items[t->dense+1],&n) || n != r)
                return ce_error(error,t->rows[r]->offset,"dense table expects ID %zu here, found %" PRIu64,r,n);
        }
        if (!unique_key(t,t->key,error)) return false;
        for (size_t k = 0; k < t->nunique; ++k) if (!unique_key(t,t->unique[k],error)) return false;
    }
    return true;
}
bool ce_tables_read(CeArena *arena, const CeAtom *data, CeTables *out, CeError *error) {
    *out = (CeTables){.arena=arena,.source=data};
    if (!form(data,"cesta-tables/1")) return ce_error(error,data->offset,"table bundle requires (cesta-tables/1 (Table ...) ...)");
    out->len = data->len-1; out->tables = ce_alloc(arena,(out->len+1)*sizeof(*out->tables));
    for (size_t i = 0; i < out->len; ++i)
        if (!read_table(arena,data->items[i+1],&out->tables[i],error)) return false;
    return ce_tables_validate(out,error);
}
bool ce_tables_load(CeArena *arena, const char *path, CeTables *out, CeError *error) {
    CeText source; CeAtom *data;
    return ce_read_file(arena,path,&source,error) && ce_atom_read(arena,source,&data,error) &&
           ce_tables_read(arena,data,out,error);
}
void ce_tables_write(const CeTables *tables, FILE *file) { ce_atom_write(tables->source,file); fputc('\n',file); }
void ce_table_rows(const CeTable *table, const size_t *columns, size_t len, FILE *file) {
    fprintf(file,"(cesta-table-rows/1 %.*s (Columns",(int)table->name.len,table->name.data);
    for (size_t c = 0; c < len; ++c) {
        CeText name = table->columns[columns[c]].name; fprintf(file," %.*s",(int)name.len,name.data);
    }
    fputs(") (Rows",file);
    for (size_t r = 0; r < table->nrows; ++r) {
        fputs(" (Row",file);
        for (size_t c = 0; c < len; ++c) { fputc(' ',file); ce_atom_write(table->rows[r]->items[columns[c]+1],file); }
        fputc(')',file);
    }
    fputs("))\n",file);
}
static void c_string(CeText text, FILE *file) {
    fputc('"',file);
    for (size_t i = 0; i < text.len; ++i) {
        unsigned c = text.data[i];
        if (c == '"' || c == '\\' || c == '?') fprintf(file,"\\%c",c);
        else if (c < 32 || c >= 127) fprintf(file,"\\%03o",c);
        else fputc((int)c,file);
    }
    fputc('"',file);
}
void ce_table_c_cell(const CeAtom *cell, const CeColumn *column, FILE *file) {
    if (column->kind == CE_COLUMN_TEXT || cell->kind == CE_ATOM_TEXT) c_string(cell->text,file);
    else if (column->kind == CE_COLUMN_NAT || (column->kind == CE_COLUMN_REF && cell->text.len &&
             cell->text.data[0] >= '0' && cell->text.data[0] <= '9'))
        fprintf(file,"UINT64_C(%.*s)",(int)cell->text.len,cell->text.data);
    else if (column->kind == CE_COLUMN_BOOL) fputs(ce_is(cell->text,"true") ? "1" : "0",file);
    else fprintf(file,"%.*s",(int)cell->text.len,cell->text.data);
}
bool ce_table_xmacro(const CeTables *tables, const CeTable *table, CeText name, const size_t *columns, size_t len,
                     FILE *file, CeError *error) {
    if (!ce_table_c_name(name)) return ce_error(error,table->offset,"row macro name requires a C declaration identifier");
    /* Avoid capture even if a logical identifier has our default formal's name. */
    char formal[64]; unsigned ordinal = 0; bool used;
    do {
        snprintf(formal,sizeof(formal),"cesta_table_apply_%u",ordinal++); used = false;
        for (size_t r = 0; r < table->nrows; ++r) for (size_t c = 0; c < len; ++c) {
            const CeAtom *a = table->rows[r]->items[columns[c]+1];
            used |= a->kind == CE_ATOM_SYMBOL && ce_is(a->text,formal);
        }
    } while (used);
    fprintf(file,"#define %.*s(%s)",(int)name.len,name.data,formal);
    for (size_t r = 0; r < table->nrows; ++r) {
        fprintf(file," \\\n    %s(",formal);
        for (size_t c = 0; c < len; ++c) {
            if (c) fputs(", ",file);
            const CeColumn *scalar = scalar_column(tables,&table->columns[columns[c]],NULL,error,table->offset);
            if (!scalar) return false;
            ce_table_c_cell(table->rows[r]->items[columns[c]+1],scalar,file);
        }
        fputc(')',file);
    }
    fputc('\n',file); return true;
}
int ce_table_command(int argc, char **argv) {
    if (argc < 4) { fputs("usage: cesta tables check|rows|header BUNDLE [TABLE [COLUMN ...]] [-o HEADER]\n",stderr); return 1; }
    CeArena arena = {0}; CeError error = {{0},0}; CeTables tables = {0};
    bool ok = ce_tables_load(&arena,argv[3],&tables,&error);
    if (ok && !strcmp(argv[2],"check") && argc == 4) ce_tables_write(&tables,stdout);
    else if (ok && !strcmp(argv[2],"rows") && argc >= 5) {
        const CeTable *t = ce_tables_find(&tables,ce_cstr(&arena,argv[4]));
        if (!t) ok = ce_error(&error,0,"unknown table");
        else {
            size_t n = argc > 5 ? (size_t)argc-5 : t->ncolumns;
            size_t *columns = ce_alloc(&arena,(n+1)*sizeof(*columns));
            for (size_t i = 0; i < n && ok; ++i) {
                columns[i] = argc > 5 ? ce_table_column(t,ce_cstr(&arena,argv[i+5])) : i;
                if (columns[i] == SIZE_MAX) ok = ce_error(&error,0,"unknown projected column");
            }
            if (ok) ce_table_rows(t,columns,n,stdout);
        }
    } else if (ok && !strcmp(argv[2],"header") && argc == 6 && !strcmp(argv[4],"-o")) {
        char *buffer = NULL; size_t length = 0; FILE *file = open_memstream(&buffer,&length);
        if (!file) ok = ce_error(&error,0,"cannot allocate header text");
        if (ok) {
            fputs("/* Generated table rows. Expand with a caller-supplied row macro. */\n#include <stdint.h>\n\n",file);
            for (size_t i = 0; i < tables.len && ok; ++i) {
                const CeTable *t = &tables.tables[i];
                size_t *columns = ce_alloc(&arena,(t->ncolumns+1)*sizeof(*columns));
                for (size_t c = 0; c < t->ncolumns; ++c) columns[c] = c;
                char *name = ce_alloc(&arena,t->name.len+20);
                snprintf(name,t->name.len+20,"CESTA_TABLE_%.*s_ROWS",(int)t->name.len,t->name.data);
                ok = ce_table_xmacro(&tables,t,ce_cstr(&arena,name),columns,t->ncolumns,file,&error);
                fputc('\n',file);
            }
            if (fclose(file)) ok = ce_error(&error,0,"cannot finish header text");
            if (ok) {
                FILE *dest = fopen(argv[5],"wb");
                if (!dest) ok = ce_error(&error,0,"cannot open table header");
                else { ok = fwrite(buffer,1,length,dest) == length; if (fclose(dest)) ok = false; }
            }
        }
        free(buffer);
    } else if (ok) ok = ce_error(&error,0,"invalid tables command or arguments");
    if (!ok) fprintf(stderr,"%s: byte %zu: %s\n",argv[3],error.offset,error.message);
    ce_arena_free(&arena); return ok ? 0 : 1;
}
