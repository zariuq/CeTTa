#define _POSIX_C_SOURCE 200809L
#include "export.h"
#include "macro.h"
#include "native.h"
#include "table.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* A cesta-export/1 manifest selects concrete instances of module definitions
   and publishes them as one ordinary C header:

     (cesta-export/1
       (header CT_SIZES_H)
       (profile gnu11)
       (include <stdint.h>)
       (use "Sizes.module" Checked)
       (function ct_doubled_capacity_u32 DoubledCapacity (count uint32_t)
         (doc "..."))
       (macro ct_probe Probe (doc "...")))

   The header is assembled as a native-c/1 document and printed by the same
   route as any Cesta unit; the native compiler still owns preprocessing. */

static bool head(const CeAtom *a, const char *name) {
    return a->kind == CE_ATOM_LIST && a->len && a->items[0]->kind == CE_ATOM_SYMBOL &&
        ce_is(a->items[0]->text,name);
}
static CeAtom *sym(CeArena *a, const char *text) {
    return ce_atom_new(a,CE_ATOM_SYMBOL,ce_cstr(a,text),0);
}
static CeAtom *txt(CeArena *a, CeText text) {
    return ce_atom_new(a,CE_ATOM_TEXT,ce_text(a,text.data,text.len),0);
}
static CeAtom *form(CeArena *a, const char *name, size_t n) {
    CeAtom *atom = ce_atom_new(a,CE_ATOM_LIST,(CeText){0},n+1);
    atom->items[0] = sym(a,name); return atom;
}
static CeText printf_text(CeArena *a, const char *format, CeText value) {
    size_t n = strlen(format)+value.len+1; char *buffer = ce_alloc(a,n);
    snprintf(buffer,n,format,(int)value.len,value.data);
    return (CeText){(unsigned char *)buffer,strlen(buffer)};
}
static bool c_identifier(CeText name) {
    if (!name.len) return false;
    for (size_t i = 0; i < name.len; ++i) {
        unsigned c = name.data[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (i && c >= '0' && c <= '9')))
            return false;
    }
    return true;
}

typedef struct {
    CeAtom **items; size_t len;
} Items;
static void push(Items *items, CeAtom *atom) {
    items->items = ce_resize(items->items,items->len+1,sizeof(*items->items));
    items->items[items->len++] = atom;
}
/* Atoms own arena storage; build them at their final length. */
static CeAtom *list_of(CeArena *a, const char *name, const Items *items) {
    CeAtom *atom = form(a,name,items->len);
    for (size_t i = 0; i < items->len; ++i) atom->items[i+1] = items->items[i];
    return atom;
}
static void push_unique(CeText **names, size_t *len, CeText name) {
    for (size_t i = 0; i < *len; ++i) if (ce_equal((*names)[i],name)) return;
    *names = ce_resize(*names,*len+1,sizeof(**names)); (*names)[(*len)++] = name;
}
static bool reserve_declaration(CeText **names, size_t *len, CeText name, size_t offset, CeError *error) {
    for (size_t i = 0; i < *len; ++i)
        if (ce_equal((*names)[i],name))
            return ce_error(error,offset,"duplicate C declaration %.*s",(int)name.len,name.data);
    *names = ce_resize(*names,*len+1,sizeof(**names)); (*names)[(*len)++] = name;
    return true;
}

static bool relative_path(CeArena *a, const char *spec, CeText name, const char **out) {
    char *path = ce_alloc(a,name.len+1); memcpy(path,name.data,name.len); path[name.len] = 0;
    if (path[0] == '/') { *out = path; return true; }
    const char *slash = strrchr(spec,'/');
    if (!slash) { *out = path; return true; }
    size_t prefix = (size_t)(slash-spec)+1; char *full = ce_alloc(a,prefix+name.len+1);
    memcpy(full,spec,prefix); memcpy(full+prefix,name.data,name.len); full[prefix+name.len] = 0;
    *out = full; return true;
}

static bool write_atomically(const char *path, CeText text, CeError *error) {
    size_t n = strlen(path)+16; char *temporary = malloc(n);
    if (!temporary) return ce_error(error,0,"cannot allocate output path");
    snprintf(temporary,n,"%s.tmp%ld",path,(long)getpid());
    FILE *file = fopen(temporary,"wb");
    bool ok = file && fwrite(text.data,1,text.len,file) == text.len;
    if (file) ok = fclose(file) == 0 && ok;
    if (ok && rename(temporary,path)) ok = false;
    if (!ok) { remove(temporary); ce_error(error,0,"cannot write %s: %s",path,strerror(errno)); }
    free(temporary); return ok;
}

/* A counter table is structured rows, (ID NAME "spelling"), with a dense
   profile: IDs run 0..n-1 in order, and names and spellings are unique. One
   table yields the enumeration and, separately, the spelling array. */
typedef struct { size_t id; CeText name, spelling; } TableRow;
typedef struct {
    CeText type, prefix, count, names;
    TableRow *rows; size_t len;
    const CeAtom *generic;
} Table;

static bool table_symbol(const CeAtom *a, const char *field, CeText *out, CeError *error) {
    if (!head(a,field) || a->len != 2 || a->items[1]->kind != CE_ATOM_SYMBOL)
        return ce_error(error,a->offset,"table field %s requires one symbol",field);
    *out = a->items[1]->text; return true;
}
static CeText c_prefixed(CeArena *arena, CeText prefix, CeText suffix) {
    unsigned char *name = ce_alloc(arena,prefix.len+suffix.len+1);
    if (prefix.len) memcpy(name,prefix.data,prefix.len);
    if (suffix.len) memcpy(name+prefix.len,suffix.data,suffix.len);
    return (CeText){name,prefix.len+suffix.len};
}
static bool c_prefixed_name(CeArena *arena, CeText prefix, CeText suffix) {
    return ce_table_c_name(c_prefixed(arena,prefix,suffix));
}
static bool read_table(CeArena *arena, const char *spec, const CeAtom *item, Table *table, CeError *error) {
    const char *path; CeText source; CeAtom *data;
    if (item->len != 2 || item->items[1]->kind != CE_ATOM_TEXT)
        return ce_error(error,item->offset,"table export requires a data filename");
    if (!relative_path(arena,spec,item->items[1]->text,&path) || !ce_read_file(arena,path,&source,error) ||
        !ce_atom_read(arena,source,&data,error)) { if (!error->offset) error->offset = item->offset; return false; }
    if (!head(data,"cesta-counters/1") || data->len != 7)
        return ce_error(error,data->offset,"counter data requires (cesta-counters/1 (enum T) (prefix P) (count C) (names N) (profile dense) (rows ...))");
    if (!table_symbol(data->items[1],"enum",&table->type,error) || !table_symbol(data->items[2],"prefix",&table->prefix,error) ||
        !table_symbol(data->items[3],"count",&table->count,error) || !table_symbol(data->items[4],"names",&table->names,error))
        return false;
    CeText profile;
    if (!table_symbol(data->items[5],"profile",&profile,error)) return false;
    if (!ce_is(profile,"dense")) return ce_error(error,data->items[5]->offset,"counter tables declare the dense profile");
    const CeAtom *rows = data->items[6];
    if (!head(rows,"rows") || rows->len < 2) return ce_error(error,rows->offset,"counter data requires nonempty (rows ...)");
    if (!ce_table_c_name(table->type) || !c_identifier(table->prefix) || !c_identifier(table->count) ||
        !c_prefixed_name(arena,table->prefix,table->count) || !ce_table_c_name(table->names))
        return ce_error(error,data->offset,"counter data names must be C identifiers");
    table->len = rows->len-1; table->rows = ce_alloc(arena,table->len*sizeof(*table->rows));
    CeAtom *bundle = form(arena,"cesta-tables/1",1);
    CeAtom *decl = form(arena,"Table",7);
    bundle->items[1] = decl; decl->items[1] = sym(arena,"Counters");
    CeAtom *columns = form(arena,"Columns",3);
    const char *names[] = {"id","name","spelling"}, *types[] = {"Nat","Text","Text"};
    for (size_t i = 0; i < 3; ++i) {
        CeAtom *column = form(arena,"Column",2);
        column->items[1] = sym(arena,names[i]); column->items[2] = sym(arena,types[i]);
        columns->items[i+1] = column;
    }
    decl->items[2] = columns;
    const char *keys[] = {"Key","Unique","Unique","Dense"};
    const char *values[] = {"id","name","spelling","id"};
    for (size_t i = 0; i < 4; ++i) {
        CeAtom *key = form(arena,keys[i],1); key->items[1] = sym(arena,values[i]); decl->items[i+3] = key;
    }
    CeAtom *shared_rows = form(arena,"Rows",table->len); decl->items[7] = shared_rows;
    for (size_t i = 0; i < table->len; ++i) {
        const CeAtom *row = rows->items[i+1]; TableRow *r = &table->rows[i];
        if (row->kind != CE_ATOM_LIST || row->len != 3 || row->items[0]->kind != CE_ATOM_SYMBOL ||
            row->items[1]->kind != CE_ATOM_SYMBOL || row->items[2]->kind != CE_ATOM_TEXT)
            return ce_error(error,row->offset,"row requires (ID NAME \"spelling\")");
        r->name = row->items[1]->text; r->spelling = row->items[2]->text;
        if (!c_identifier(r->name)) return ce_error(error,row->offset,"row name must be a C identifier suffix");
        if (!c_prefixed_name(arena,table->prefix,r->name))
            return ce_error(error,row->offset,"row name must produce a C declaration identifier");
        if (ce_equal(r->name,table->count)) return ce_error(error,row->offset,"row name collides with the count");
        for (size_t k = 0; k < r->spelling.len; ++k) {
            unsigned c = r->spelling.data[k];
            if (c < 32 || c > 126 || c == '"' || c == '\\')
                return ce_error(error,row->offset,"row spelling must be printable ASCII without quotes or backslashes");
        }
        CeAtom *shared = form(arena,"Row",3); shared->offset = row->offset;
        for (size_t k = 0; k < 3; ++k) shared->items[k+1] = k == 1 ? txt(arena,row->items[k]->text) : row->items[k];
        shared_rows->items[i+1] = shared;
    }
    CeTables shared;
    if (!ce_tables_read(arena,bundle,&shared,error)) return false;
    table->generic = bundle;
    for (size_t i = 0; i < table->len; ++i) table->rows[i].id = i;

    return true;
}
static CeAtom *table_enum(CeArena *arena, const Table *table) {
    char *text = NULL; size_t length = 0; FILE *file = open_memstream(&text,&length);
    if (!file) return NULL;
    fputs("typedef enum {\n",file);
    for (size_t i = 0; i < table->len; ++i)
        fprintf(file,"  %.*s%.*s = %zu,\n",(int)table->prefix.len,table->prefix.data,
                (int)table->rows[i].name.len,table->rows[i].name.data,table->rows[i].id);
    fprintf(file,"  %.*s%.*s = %zu\n} %.*s;",(int)table->prefix.len,table->prefix.data,
            (int)table->count.len,table->count.data,table->len,(int)table->type.len,table->type.data);
    if (fclose(file)) { free(text); return NULL; }
    CeAtom *block = form(arena,"native",1); block->items[1] = txt(arena,ce_text(arena,text,length));
    free(text); return block;
}
static CeAtom *table_names(CeArena *arena, const Table *table) {
    char *text = NULL; size_t length = 0; FILE *file = open_memstream(&text,&length);
    if (!file) return NULL;
    fprintf(file,"static const char *const %.*s[%.*s%.*s] = {\n",(int)table->names.len,table->names.data,
            (int)table->prefix.len,table->prefix.data,(int)table->count.len,table->count.data);
    for (size_t i = 0; i < table->len; ++i) {
        fprintf(file,"  [%.*s%.*s] = \"",(int)table->prefix.len,table->prefix.data,
                (int)table->rows[i].name.len,table->rows[i].name.data);
        /* Escape question marks so translation-phase trigraph replacement
           cannot change a printable spelling in C11. */
        for (size_t j = 0; j < table->rows[i].spelling.len; ++j) {
            unsigned c = table->rows[i].spelling.data[j];
            if (c == '?') fputc('\\',file);
            fputc((int)c,file);
        }
        fputs("\",\n",file);
    }
    fputs("};",file);
    if (fclose(file)) { free(text); return NULL; }
    CeAtom *block = form(arena,"native",1); block->items[1] = txt(arena,ce_text(arena,text,length));
    free(text); return block;
}

/* Generic projections share the checked table schema with counter and operation data. */
static bool table_export(CeArena *arena, const char *spec, const CeAtom *item, FILE *plan,
                         CeText **declared, size_t *ndeclared,
                         CeText *exported, CeAtom **out, CeError *error) {
    const char *path;
    if (item->len < 4 || item->items[1]->kind != CE_ATOM_TEXT || item->items[2]->kind != CE_ATOM_SYMBOL ||
        !relative_path(arena,spec,item->items[1]->text,&path))
        return ce_error(error,item->offset,"table export requires FILE TABLE and named options");
    CeTables bundle;
    if (!ce_tables_load(arena,path,&bundle,error)) return false;
    const CeTable *table = ce_tables_find(&bundle,item->items[2]->text);
    if (!table) return ce_error(error,item->offset,"export names an unknown table");
    CeText name = {0}, prefix = {0}, count = {0};
    size_t id = SIZE_MAX, label = SIZE_MAX, ncolumns = table->ncolumns;
    size_t *columns = ce_alloc(arena,(table->ncolumns+item->len+1)*sizeof(*columns));
    for (size_t i = 0; i < ncolumns; ++i) columns[i] = i;
    size_t *where = ce_alloc(arena,item->len*sizeof(*where));
    const CeAtom **values = ce_alloc(arena,item->len*sizeof(*values)); size_t nwhere = 0;
    bool is_enum = head(item,"table-enum"), is_names = head(item,"table-names");
    bool seen[9] = {0};
    const char *const options[] = {"enum","array","macro","prefix","count","id","name","text","columns","where"};
    for (size_t i = 3; i < item->len; ++i) {
        const CeAtom *a = item->items[i]; size_t option = 0;
        while (option < 10 && !head(a,options[option])) ++option;
        if (option == 10) return ce_error(error,a->offset,"unknown table export option");
        if (option < 9 && seen[option]) return ce_error(error,a->offset,"table export repeats an option");
        if (option < 9) seen[option] = true;
        if (option <= 4) {
            if (a->len != 2 || a->items[1]->kind != CE_ATOM_SYMBOL || !c_identifier(a->items[1]->text))
                return ce_error(error,a->offset,"export name requires one C identifier");
            if (option != 3 && !ce_table_c_name(a->items[1]->text))
                return ce_error(error,a->offset,"export name requires a C declaration identifier");
            if (option <= 2) {
                if ((is_enum && option != 0) || (is_names && option != 1) || (!is_enum && !is_names && option != 2) || name.len)
                    return ce_error(error,a->offset,"export name option does not match its projection");
                name = a->items[1]->text;
            } else if (option == 3) prefix = a->items[1]->text;
            else count = a->items[1]->text;
        } else if (option <= 7) {
            if (a->len != 2 || a->items[1]->kind != CE_ATOM_SYMBOL)
                return ce_error(error,a->offset,"column option requires one column name");
            size_t column = ce_table_column(table,a->items[1]->text);
            if (column == SIZE_MAX) return ce_error(error,a->offset,"export names an unknown column");
            if (option == 5) id = column;
            else {
                if (label != SIZE_MAX) return ce_error(error,a->offset,"export repeats its label column");
                label = column;
            }
        } else if (option == 8) {
            ncolumns = a->len-1; columns = ce_alloc(arena,(ncolumns+1)*sizeof(*columns));
            for (size_t j = 0; j < ncolumns; ++j) {
                columns[j] = a->items[j+1]->kind == CE_ATOM_SYMBOL ? ce_table_column(table,a->items[j+1]->text) : SIZE_MAX;
                if (columns[j] == SIZE_MAX) return ce_error(error,a->offset,"projection names an unknown column");
            }
        } else {
            if (a->len != 3 || a->items[1]->kind != CE_ATOM_SYMBOL)
                return ce_error(error,a->offset,"filter is (where COLUMN VALUE)");
            where[nwhere] = ce_table_column(table,a->items[1]->text); values[nwhere] = a->items[2];
            if (where[nwhere] == SIZE_MAX || a->items[2]->kind == CE_ATOM_LIST)
                return ce_error(error,a->offset,"filter names an unknown column or nonscalar value");
            if (!ce_table_cell_type(&bundle,&table->columns[where[nwhere]],values[nwhere],error)) return false;
            ++nwhere;
        }
    }
    if (!name.len) return ce_error(error,item->offset,"table export requires its C name");
    if (!reserve_declaration(declared,ndeclared,name,item->offset,error)) return false;
    if ((is_enum && (seen[1] || seen[2] || seen[7] || seen[8])) ||
        (is_names && (seen[0] || seen[2] || seen[3] || seen[6] || seen[8])) ||
        (!is_enum && !is_names && (seen[0] || seen[1] || seen[3] || seen[4] || seen[5] || seen[6] || seen[7])))
        return ce_error(error,item->offset,"table export option does not apply to its projection");
    CeTable selected = *table;
    selected.rows = ce_alloc(arena,(table->nrows+1)*sizeof(*selected.rows)); selected.nrows = 0;
    for (size_t r = 0; r < table->nrows; ++r) {
        bool match = true;
        for (size_t w = 0; w < nwhere; ++w) {
            const CeAtom *cell = table->rows[r]->items[where[w]+1];
            match &= cell->kind == values[w]->kind && ce_equal(cell->text,values[w]->text);
        }
        if (match) selected.rows[selected.nrows++] = table->rows[r];
    }
    char *buffer = NULL; size_t length = 0; FILE *file = open_memstream(&buffer,&length);
    if (!file) return ce_error(error,item->offset,"cannot allocate table projection");
    bool ok = true;
    if (is_enum || is_names) {
        if (is_enum && !selected.nrows && !count.len)
            ok = ce_error(error,item->offset,"empty C enum projection requires a count enumerator");
        if (id == SIZE_MAX || label == SIZE_MAX || table->columns[id].kind != CE_COLUMN_NAT)
            ok = ce_error(error,item->offset,"enum/names export requires Nat id and label columns");
        if (is_names && ok && (nwhere || table->dense != id || table->columns[label].kind != CE_COLUMN_TEXT || !count.len || !selected.nrows))
            ok = ce_error(error,item->offset,"name array requires a nonempty unfiltered dense id, Text column and count name");
        if (is_enum && ok) fprintf(file,"typedef enum {\n");
        if (is_names && ok) fprintf(file,"static const char *const %.*s[%.*s] = {\n",(int)name.len,name.data,(int)count.len,count.data);
        for (size_t r = 0; r < selected.nrows && ok; ++r) {
            const CeAtom *num = selected.rows[r]->items[id+1], *text = selected.rows[r]->items[label+1];
            uint64_t value = 0; (void)ce_table_nat(num,&value);
            if (value > INT32_MAX) { ok = ce_error(error,num->offset,"C11 enum projection needs values representable by int; use row export for uint64 IDs"); break; }
            if (is_enum) {
                if (text->kind == CE_ATOM_LIST || !c_identifier(text->text) || !c_prefixed_name(arena,prefix,text->text)) {
                    ok = ce_error(error,text->offset,"enum suffix requires a C identifier"); break;
                }
                for (size_t k = 0; k < r; ++k)
                    if (ce_equal(selected.rows[k]->items[label+1]->text,text->text))
                        ok = ce_error(error,text->offset,"enum projection repeats an enumerator");
                if (!ok) break;
                if (!reserve_declaration(declared,ndeclared,c_prefixed(arena,prefix,text->text),text->offset,error)) {
                    ok = false; break;
                }
                if (count.len == prefix.len+text->text.len &&
                    (!prefix.len || !memcmp(count.data,prefix.data,prefix.len)) &&
                    !memcmp(count.data+prefix.len,text->text.data,text->text.len)) {
                    ok = ce_error(error,text->offset,"enum row name collides with the count"); break;
                }
                fprintf(file,"    %.*s%.*s = %" PRIu64 ",\n",(int)prefix.len,prefix.data,(int)text->text.len,text->text.data,value);
            } else {
                fprintf(file,"    [%" PRIu64 "] = ",value); ce_table_c_cell(text,&table->columns[label],file); fputs(",\n",file);
            }
        }
        if (ok && is_enum) {
            if (count.len) {
                if (selected.nrows > INT32_MAX) ok = ce_error(error,item->offset,"C11 count enumerator must fit int");
                else ok = reserve_declaration(declared,ndeclared,count,item->offset,error);
                if (ok) fprintf(file,"    %.*s = %zu\n",(int)count.len,count.data,selected.nrows);
            }
            fprintf(file,"} %.*s;\n",(int)name.len,name.data);
        } else if (ok) fputs("};\n",file);
    } else ok = ce_table_xmacro(&bundle,&selected,name,columns,ncolumns,file,error);
    if (fclose(file)) ok = ce_error(error,item->offset,"cannot finish table projection");
    if (ok) {
        if (plan) {
            ce_tables_write(&bundle,plan);
            fprintf(plan,"(cesta-table-view/1 %.*s (Columns",(int)table->name.len,table->name.data);
            for (size_t c = 0; c < ncolumns; ++c) {
                CeText n = table->columns[columns[c]].name; fprintf(plan," %.*s",(int)n.len,n.data);
            }
            fputs(") (Where",plan);
            for (size_t w = 0; w < nwhere; ++w) {
                CeText n = table->columns[where[w]].name; fprintf(plan," (%.*s ",(int)n.len,n.data);
                ce_atom_write(values[w],plan); fputc(')',plan);
            }
            fputs(") (Rows",plan);
            for (size_t r = 0; r < selected.nrows; ++r) {
                fputs(" (Row",plan);
                for (size_t c = 0; c < ncolumns; ++c) { fputc(' ',plan); ce_atom_write(selected.rows[r]->items[columns[c]+1],plan); }
                fputc(')',plan);
            }
            fputs("))\n",plan);
        }
        *exported = name; *out = form(arena,"native",1); (*out)->items[1] = txt(arena,ce_text(arena,buffer,length));
    }
    free(buffer); return ok;
}

bool ce_export_header(const CeGrammar *g, CeArena *arena, const char *spec, FILE *plan, CeText *out, CeError *error) {
    CeText source; CeAtom *manifest;
    if (!ce_read_file(arena,spec,&source,error) || !ce_atom_read(arena,source,&manifest,error)) return false;
    if (!head(manifest,"cesta-export/1"))
        return ce_error(error,manifest->offset,"export specification requires (cesta-export/1 ...)");
    const CeAtom *guard = NULL; bool gnu = true; Items includes = {0}, entries = {0};
    CeMacros *macros = ce_macros_new(g,NULL,error); size_t begin = 0;
    CeText *protect = NULL, *builtins = NULL, *exported = NULL;
    CeText *declared = NULL; size_t ndeclared = 0;
    size_t nprotect = 0, nbuiltins = 0, nexported = 0; bool ok = macros != NULL, theory = false, table_rows = false;
    for (size_t i = 1; i < manifest->len && ok; ++i) {
        const CeAtom *item = manifest->items[i];
        if (head(item,"header")) {
            if (guard || item->len != 2 || item->items[1]->kind != CE_ATOM_SYMBOL || !c_identifier(item->items[1]->text))
                ok = ce_error(error,item->offset,"header requires one include-guard identifier");
            else guard = item->items[1];
        } else if (head(item,"profile")) {
            if (item->len != 2 || item->items[1]->kind != CE_ATOM_SYMBOL ||
                (!ce_is(item->items[1]->text,"gnu11") && !ce_is(item->items[1]->text,"c11")))
                ok = ce_error(error,item->offset,"profile is gnu11 or c11");
            else gnu = ce_is(item->items[1]->text,"gnu11");
        } else if (head(item,"include")) {
            push(&includes,(CeAtom *)item);
        } else if (head(item,"use")) {
            const char *path;
            if (theory) ok = ce_error(error,item->offset,"an export specification uses one theory");
            else if (item->len != 3 || item->items[1]->kind != CE_ATOM_TEXT || item->items[2]->kind != CE_ATOM_SYMBOL)
                ok = ce_error(error,item->offset,"use requires a module filename and theory name");
            else if (relative_path(arena,spec,item->items[1]->text,&path)) {
                begin = ce_macros_rules(macros); theory = true;
                ok = ce_macros_load(macros,path,(const char *)item->items[2]->text.data,error);
                if (!ok && !error->offset) error->offset = item->offset;
            }
        } else if (head(item,"function") || head(item,"macro")) {
            bool function = head(item,"function");
            if (!theory) { ok = ce_error(error,item->offset,"use must precede exported definitions"); break; }
            if (item->len < 3 || item->items[1]->kind != CE_ATOM_SYMBOL || item->items[2]->kind != CE_ATOM_SYMBOL ||
                !c_identifier(item->items[1]->text)) {
                ok = ce_error(error,item->offset,"export requires a C name and a constructor"); break;
            }
            CeText name = item->items[1]->text; const CeAtom *doc = NULL;
            for (size_t j = 0; j < nexported; ++j)
                if (ce_equal(exported[j],name)) { ok = ce_error(error,item->offset,"duplicate exported name"); break; }
            if (!ok) break;
            Items pairs = {0};
            for (size_t j = 3; j < item->len; ++j) {
                const CeAtom *field = item->items[j];
                if (head(field,"doc") && field->len == 2 && field->items[1]->kind == CE_ATOM_TEXT && !doc) doc = field->items[1];
                else if (field->kind == CE_ATOM_LIST && field->len == 2 && field->items[0]->kind == CE_ATOM_SYMBOL)
                    push(&pairs,(CeAtom *)field);
                else { ok = ce_error(error,field->offset,"expected (PARAMETER TYPE) or (doc TEXT)"); break; }
            }
            CeAtom *types = ce_atom_new(arena,CE_ATOM_LIST,(CeText){0},pairs.len);
            for (size_t j = 0; j < pairs.len; ++j) types->items[j] = pairs.items[j];
            free(pairs.items);
            if (!ok) break;
            CeMacroExport result = {0};
            CeMacroExportRequest request = {.name=name,.constructor=item->items[2]->text,.types=types,
                .function=function,.offset=item->offset,.plan=plan};
            ok = ce_macros_export(macros,begin,ce_macros_rules(macros),&request,arena,&result,error);
            if (!ok) break;
            exported = ce_resize(exported,nexported+1,sizeof(*exported)); exported[nexported++] = name;
            for (size_t j = 0; j < result.nprotected; ++j) push_unique(&protect,&nprotect,result.protected_names[j]);
            for (size_t j = 0; j < result.nbuiltins; ++j) push_unique(&builtins,&nbuiltins,result.builtins[j]);
            if (!reserve_declaration(&declared,&ndeclared,item->items[1]->text,item->offset,error)) { ok = false; break; }
            if (doc) {
                for (size_t j = 1; j < doc->text.len; ++j)
                    if (doc->text.data[j-1] == '*' && doc->text.data[j] == '/') { ok = ce_error(error,doc->offset,"documentation contains */"); break; }
                if (!ok) break;
                CeAtom *comment = form(arena,"native",1);
                comment->items[1] = txt(arena,printf_text(arena,"\n/* %.*s */",doc->text)); push(&entries,comment);
            }
            push(&entries,result.definition);
        } else if (head(item,"counters") || head(item,"counter-names")) {
            Table table = {0};
            if (!(ok = read_table(arena,spec,item,&table,error))) break;
            CeText name = head(item,"counters") ? table.type : table.names;
            if (!reserve_declaration(&declared,&ndeclared,name,item->offset,error)) { ok = false; break; }
            if (head(item,"counters")) {
                for (size_t r = 0; r < table.len && ok; ++r)
                    ok = reserve_declaration(&declared,&ndeclared,c_prefixed(arena,table.prefix,table.rows[r].name),item->offset,error);
                if (ok) ok = reserve_declaration(&declared,&ndeclared,c_prefixed(arena,table.prefix,table.count),item->offset,error);
                if (!ok) break;
            }
            for (size_t j = 0; j < nexported; ++j)
                if (ce_equal(exported[j],name)) { ok = ce_error(error,item->offset,"duplicate exported name"); break; }
            if (!ok) break;
            exported = ce_resize(exported,nexported+1,sizeof(*exported)); exported[nexported++] = name;
            CeAtom *block = head(item,"counters") ? table_enum(arena,&table) : table_names(arena,&table);
            if (!block) { ok = ce_error(error,item->offset,"cannot render counter table"); break; }
            if (plan) { ce_atom_write(table.generic,plan); fputc('\n',plan); }
            CeAtom *spacer = form(arena,"native",1); spacer->items[1] = txt(arena,ce_cstr(arena,""));
            push(&entries,spacer); push(&entries,block);
        } else if (head(item,"table-enum") || head(item,"table-names") || head(item,"table-rows")) {
            CeText name = {0}; CeAtom *block = NULL;
            if (!(ok = table_export(arena,spec,item,plan,&declared,&ndeclared,&name,&block,error))) break;
            table_rows |= head(item,"table-rows");
            for (size_t j = 0; j < nexported; ++j)
                if (ce_equal(exported[j],name)) { ok = ce_error(error,item->offset,"duplicate exported name"); break; }
            if (!ok) break;
            exported = ce_resize(exported,nexported+1,sizeof(*exported)); exported[nexported++] = name;
            push(&entries,block);
        } else ok = ce_error(error,item->offset,"unknown export specification field");
    }
    if (ok && !guard) ok = ce_error(error,manifest->offset,"export specification requires (header GUARD)");
    for (size_t i = 0; ok && i < ndeclared; ++i)
        if (ce_equal(guard->text,declared[i])) ok = ce_error(error,guard->offset,"include guard collides with a C declaration");
    if (ok && !nexported) ok = ce_error(error,manifest->offset,"export specification exports nothing");
    if (ok && !gnu && nbuiltins) ok = ce_error(error,manifest->offset,"compiler builtins require the gnu11 profile");
    if (ok) {
        /* Directives are grouped text; definitions stay structured and are
           printed by the C printer, each under its documentation. */
        char *prologue = NULL; size_t prologue_length = 0; FILE *text = open_memstream(&prologue,&prologue_length);
        if (!text) ok = ce_error(error,0,"cannot allocate export prologue");
        CeText name = guard ? guard->text : (CeText){0};
        if (ok) fprintf(text,"#ifndef %.*s\n#define %.*s 1\n",(int)name.len,name.data,(int)name.len,name.data);
        if (ok && includes.len) fputc('\n',text);
        bool has_stdint = false;
        for (size_t i = 0; ok && i < includes.len; ++i) {
            const CeAtom *value = includes.items[i]->len == 2 ? includes.items[i]->items[1] : NULL;
            has_stdint |= value && value->kind == CE_ATOM_SYMBOL && ce_is(value->text,"<stdint.h>");
            if (value && value->kind == CE_ATOM_TEXT && !memchr(value->text.data,'"',value->text.len) &&
                !memchr(value->text.data,'\\',value->text.len) && !memchr(value->text.data,'\n',value->text.len))
                fprintf(text,"#include \"%.*s\"\n",(int)value->text.len,value->text.data);
            else if (value && value->kind == CE_ATOM_SYMBOL && value->text.len > 2 && value->text.data[0] == '<' &&
                     value->text.data[value->text.len-1] == '>')
                fprintf(text,"#include %.*s\n",(int)value->text.len,value->text.data);
            else ok = ce_error(error,includes.items[i]->offset,"include requires <header> or \"header\"");
        }
        if (ok && table_rows && !has_stdint) fputs("\n#include <stdint.h>\n",text);
        if (ok) {
            fputs("\n#if ",text);
            for (size_t i = 0; i < nexported; ++i)
                fprintf(text,"%sdefined(%.*s)",i ? " || " : "",(int)exported[i].len,exported[i].data);
            fputs("\n#error an exported name is defined as a preprocessor macro\n#endif\n",text);
        }
        if (ok && nbuiltins) {
            fputs("#if ",text);
            for (size_t i = 0; i < nbuiltins; ++i)
                fprintf(text,"%sdefined(%.*s)",i ? " || " : "",(int)builtins[i].len,builtins[i].data);
            fputs("\n#error a required compiler builtin is defined as a preprocessor macro\n#endif\n"
                  "#if !defined(__has_builtin)\n#error the compiler cannot report its builtins\n#elif ",text);
            for (size_t i = 0; i < nbuiltins; ++i)
                fprintf(text,"%s!__has_builtin(%.*s)",i ? " || " : "",(int)builtins[i].len,builtins[i].data);
            fputs("\n#error the compiler does not provide a required builtin\n#endif\n",text);
        }
        if (ok && nprotect) {
            fputs("\n/* Parameter and local names below are this header's own, whatever macros the includer defines. */\n",text);
            for (size_t i = 0; i < nprotect; ++i)
                fprintf(text,"#pragma push_macro(\"%.*s\")\n#undef %.*s\n",(int)protect[i].len,protect[i].data,
                        (int)protect[i].len,protect[i].data);
        }
        if (text) ok = fclose(text) == 0 && ok;
        Items body = {0};
        if (ok) {
            CeAtom *block = form(arena,"native",1); block->items[1] = txt(arena,ce_text(arena,prologue,prologue_length-1));
            push(&body,block);
        }
        free(prologue);
        for (size_t i = 0; ok && i < entries.len; ++i) push(&body,entries.items[i]);
        char *epilogue = NULL; size_t epilogue_length = 0; FILE *tail = ok ? open_memstream(&epilogue,&epilogue_length) : NULL;
        if (ok && !tail) ok = ce_error(error,0,"cannot allocate export epilogue");
        if (ok) {
            fputc('\n',tail);
            for (size_t i = 0; i < nprotect; ++i)
                fprintf(tail,"#pragma pop_macro(\"%.*s\")\n",(int)protect[i].len,protect[i].data);
            fprintf(tail,"%s#endif",nprotect ? "\n" : "");
            ok = fclose(tail) == 0;
            CeAtom *block = form(arena,"native",1); block->items[1] = txt(arena,ce_text(arena,epilogue,epilogue_length));
            push(&body,block);
        }
        free(epilogue);
        const char *base = strrchr(spec,'/'); base = base ? base+1 : spec;
        CeAtom *banner = form(arena,"comment",1);
        banner->items[1] = txt(arena,printf_text(arena," Generated by cesta export-header from %.*s. Edit the specification or its modules and regenerate. ",
            ce_cstr(arena,base)));
        Items unit_items = {0}; push(&unit_items,banner);
        for (size_t i = 0; i < body.len; ++i) push(&unit_items,body.items[i]);
        CeAtom *unit = list_of(arena,"unit",&unit_items); free(unit_items.items);
        CeAtom *document = form(arena,"cesta",3);
        document->items[1] = sym(arena,"native-c/1"); document->items[2] = sym(arena,"compact-c/2"); document->items[3] = unit;
        char *buffer = NULL; size_t length = 0; FILE *file = ok ? open_memstream(&buffer,&length) : NULL;
        if (ok && !file) ok = ce_error(error,0,"cannot allocate export document");
        else if (ok) {
            ce_atom_write(document,file); ok = fclose(file) == 0;
            CeNativeOutput rendered = {0};
            if (ok) ok = ce_native_text_at(g,arena,ce_text(arena,buffer,length),NULL,spec,NULL,&rendered,error);
            if (ok) *out = rendered.text;
            free(buffer);
        }
        free(body.items);
    }
    free(includes.items); free(entries.items); free(protect); free(builtins); free(exported); free(declared);
    ce_macros_free(macros); return ok;
}

int ce_export_command(int argc, char **argv) {
    const char *spec = NULL, *output = NULL, *plan_path = NULL; CeError error = {{0},0};
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i],"-o") && i+1 < argc) output = argv[++i];
        else if (!strcmp(argv[i],"--plan") && i+1 < argc) plan_path = argv[++i];
        else if (!spec) spec = argv[i];
        else spec = NULL, i = argc;
    }
    if (!spec || !output) { fputs("usage: cesta export-header [--plan PLAN] EXPORT-SPEC -o OUTPUT.h\n",stderr); return 1; }
    FILE *plan = NULL;
    if (plan_path && !(plan = fopen(plan_path,"w"))) { fprintf(stderr,"cesta: cannot write %s\n",plan_path); return 1; }
    CeArena arena = {0}; CeGrammar grammar = {0}; CeText text; int result = 1;
    if (ce_builtin_language(&grammar,true,&error) && ce_export_header(&grammar,&arena,spec,plan,&text,&error) &&
        write_atomically(output,text,&error)) result = 0;
    else fprintf(stderr,"%s: byte %zu: %s\n",spec,error.offset,error.message);
    if (plan && fclose(plan) && !result) { fprintf(stderr,"cesta: cannot write %s\n",plan_path); result = 1; }
    ce_grammar_free(&grammar); ce_arena_free(&arena); return result;
}
