#define _GNU_SOURCE
#include "atom.h"
#include "lang.h"
#include "parser.h"
#include "symbol.h"

#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} TextBuf;

typedef struct {
    VarId old_id;
    SymbolId old_spelling;
    Atom *replacement;
} VarAlias;

typedef struct {
    VarAlias *items;
    uint32_t len;
    uint32_t cap;
} VarAliasMap;

static void usage(FILE *out) {
    fputs("usage: cetta-oracle-diff --expected-file <path> "
          "(--actual-file <path> | --actual-text <text>) "
          "[--actual-label <label>]\n", out);
}

static bool text_buf_reserve(TextBuf *buf, size_t needed) {
    if (needed <= buf->cap)
        return true;
    size_t next = buf->cap ? buf->cap * 2 : 256;
    while (next < needed)
        next *= 2;
    char *data = realloc(buf->data, next);
    if (!data)
        return false;
    buf->data = data;
    buf->cap = next;
    return true;
}

static bool text_buf_append_span(TextBuf *buf, const char *start, size_t len) {
    if (!text_buf_reserve(buf, buf->len + len + 1))
        return false;
    memcpy(buf->data + buf->len, start, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

static bool text_buf_append_char(TextBuf *buf, char ch) {
    return text_buf_append_span(buf, &ch, 1);
}

static char *read_file_text(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    TextBuf buf = {0};
    char tmp[4096];
    size_t n = 0;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) {
        if (!text_buf_append_span(&buf, tmp, n)) {
            fclose(f);
            free(buf.data);
            return NULL;
        }
    }
    if (ferror(f)) {
        fclose(f);
        free(buf.data);
        return NULL;
    }
    fclose(f);
    if (!buf.data) {
        buf.data = malloc(1);
        if (!buf.data)
            return NULL;
        buf.data[0] = '\0';
    }
    return buf.data;
}

static const char *trim_left(const char *start, const char *end) {
    while (start < end && isspace((unsigned char)*start))
        start++;
    return start;
}

static const char *trim_right(const char *start, const char *end) {
    while (end > start && isspace((unsigned char)end[-1]))
        end--;
    return end;
}

static bool span_eq_cstr(const char *start, const char *end, const char *s) {
    size_t len = (size_t)(end - start);
    return strlen(s) == len && memcmp(start, s, len) == 0;
}

static bool append_atom_segment(TextBuf *out, const char *start,
                                const char *end) {
    start = trim_left(start, end);
    end = trim_right(start, end);
    if (start == end)
        return true;
    return text_buf_append_span(out, start, (size_t)(end - start)) &&
           text_buf_append_char(out, '\n');
}

static bool append_result_set_items(TextBuf *out, const char *start,
                                    const char *end) {
    const char *segment = start;
    int paren_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char *p = start; p < end; p++) {
        char ch = *p;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '(') {
            paren_depth++;
            continue;
        }
        if (ch == ')') {
            if (paren_depth > 0)
                paren_depth--;
            continue;
        }
        if (ch == '[') {
            bracket_depth++;
            continue;
        }
        if (ch == ']') {
            if (bracket_depth > 0)
                bracket_depth--;
            continue;
        }
        if (ch == ',' && paren_depth == 0 && bracket_depth == 0) {
            if (!append_atom_segment(out, segment, p))
                return false;
            segment = p + 1;
        }
    }
    return append_atom_segment(out, segment, end);
}

static char *prepare_oracle_output(const char *raw) {
    TextBuf out = {0};
    const char *line = raw;
    while (line && *line) {
        const char *next = strchr(line, '\n');
        const char *end = next ? next : line + strlen(line);
        if (end > line && end[-1] == '\r')
            end--;
        const char *trimmed_start = trim_left(line, end);
        const char *trimmed_end = trim_right(trimmed_start, end);
        if (trimmed_start != trimmed_end &&
            !span_eq_cstr(trimmed_start, trimmed_end, "MORK init: done")) {
            if (*trimmed_start == '[' && trimmed_end[-1] == ']') {
                if (!append_result_set_items(&out, trimmed_start + 1,
                                             trimmed_end - 1)) {
                    free(out.data);
                    return NULL;
                }
            } else if (!append_atom_segment(&out, trimmed_start, trimmed_end)) {
                free(out.data);
                return NULL;
            }
        }
        line = next ? next + 1 : NULL;
    }
    if (!out.data) {
        out.data = malloc(1);
        if (!out.data)
            return NULL;
        out.data[0] = '\0';
    }
    return out.data;
}

static Atom *copy_alpha_normalized(Arena *arena, Atom *atom, VarAliasMap *map);

static Atom *var_alias_lookup_or_add(Arena *arena, Atom *var, VarAliasMap *map) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].old_id == var->var_id &&
            map->items[i].old_spelling == var->sym_id) {
            return map->items[i].replacement;
        }
    }
    if (map->len == map->cap) {
        uint32_t next = map->cap ? map->cap * 2 : 8;
        VarAlias *items = realloc(map->items, sizeof(VarAlias) * next);
        if (!items)
            return NULL;
        map->items = items;
        map->cap = next;
    }
    char name[32];
    snprintf(name, sizeof(name), "_%u", map->len);
    Atom *replacement = atom_var_with_id(arena, name, (VarId)(map->len + 1));
    map->items[map->len++] = (VarAlias){
        .old_id = var->var_id,
        .old_spelling = var->sym_id,
        .replacement = replacement,
    };
    return replacement;
}

static Atom *copy_grounded(Arena *arena, Atom *atom) {
    switch (atom->ground.gkind) {
    case GV_INT:
        return atom_int(arena, atom->ground.ival);
    case GV_FLOAT:
        return atom_float(arena, atom->ground.fval);
    case GV_BOOL:
        return atom_bool(arena, atom->ground.bval);
    case GV_STRING:
        return atom_string(arena, atom->ground.sval);
    case GV_SPACE:
        return atom_space(arena, atom->ground.ptr);
    case GV_STATE:
        return atom_state(arena, (StateCell *)atom->ground.ptr);
    case GV_CAPTURE:
        return atom_capture(arena, (CaptureClosure *)atom->ground.ptr);
    case GV_FOREIGN:
        return atom_foreign(arena, (CettaForeignValue *)atom->ground.ptr);
    }
    return atom;
}

static Atom *copy_alpha_normalized(Arena *arena, Atom *atom, VarAliasMap *map) {
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return atom_symbol_id(arena, atom->sym_id);
    case ATOM_VAR:
        return var_alias_lookup_or_add(arena, atom, map);
    case ATOM_GROUNDED:
        return copy_grounded(arena, atom);
    case ATOM_EXPR: {
        Atom **elems =
            atom->expr.len > 0 ? arena_alloc(arena, sizeof(Atom *) * atom->expr.len)
                               : NULL;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            elems[i] = copy_alpha_normalized(arena, atom->expr.elems[i], map);
            if (!elems[i])
                return NULL;
        }
        return atom_expr(arena, elems, atom->expr.len);
    }
    }
    return NULL;
}

static Atom *alpha_normalize_atom(Arena *arena, Atom *atom) {
    VarAliasMap map = {0};
    Atom *normalized = copy_alpha_normalized(arena, atom, &map);
    free(map.items);
    return normalized;
}

static bool atom_is_numeric(Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
           (atom->ground.gkind == GV_INT || atom->ground.gkind == GV_FLOAT);
}

static long double atom_numeric_value(Atom *atom) {
    return atom->ground.gkind == GV_INT ? (long double)atom->ground.ival
                                        : (long double)atom->ground.fval;
}

static int compare_numeric_atoms(Atom *lhs, Atom *rhs) {
    if (lhs->ground.gkind == GV_FLOAT && isnan(lhs->ground.fval)) {
        if (rhs->ground.gkind == GV_FLOAT && isnan(rhs->ground.fval))
            return 0;
        return 1;
    }
    if (rhs->ground.gkind == GV_FLOAT && isnan(rhs->ground.fval))
        return -1;
    long double lhs_value = atom_numeric_value(lhs);
    long double rhs_value = atom_numeric_value(rhs);
    if (lhs_value < rhs_value)
        return -1;
    if (lhs_value > rhs_value)
        return 1;
    return 0;
}

static int oracle_atom_compare(Atom *lhs, Atom *rhs) {
    if (lhs == rhs)
        return 0;
    if (!lhs)
        return -1;
    if (!rhs)
        return 1;
    if (atom_is_numeric(lhs) && atom_is_numeric(rhs))
        return compare_numeric_atoms(lhs, rhs);
    if (lhs->kind != rhs->kind)
        return atom_total_order(lhs, rhs);
    if (lhs->kind != ATOM_EXPR)
        return atom_total_order(lhs, rhs);
    uint32_t shared = lhs->expr.len < rhs->expr.len ? lhs->expr.len : rhs->expr.len;
    for (uint32_t i = 0; i < shared; i++) {
        int cmp = oracle_atom_compare(lhs->expr.elems[i], rhs->expr.elems[i]);
        if (cmp != 0)
            return cmp;
    }
    if (lhs->expr.len < rhs->expr.len)
        return -1;
    if (lhs->expr.len > rhs->expr.len)
        return 1;
    return 0;
}

static int qsort_atom_compare(const void *lhs, const void *rhs) {
    Atom *const *a = lhs;
    Atom *const *b = rhs;
    return oracle_atom_compare(*a, *b);
}

static bool parse_output_atoms(const char *label,
                               const char *raw,
                               Arena *arena,
                               Atom ***out_atoms,
                               int *out_len) {
    char *prepared = prepare_oracle_output(raw);
    if (!prepared) {
        fprintf(stderr, "cetta-oracle-diff: could not normalize %s output\n", label);
        return false;
    }
    Atom **parsed = NULL;
    int n = parse_metta_text(prepared, arena, &parsed);
    if (n < 0) {
        fprintf(stderr, "cetta-oracle-diff: could not parse %s output\n", label);
        fprintf(stderr, "--- normalized %s output ---\n%s", label, prepared);
        free(prepared);
        return false;
    }
    free(prepared);
    Atom **normalized = n > 0 ? arena_alloc(arena, sizeof(Atom *) * (size_t)n) : NULL;
    for (int i = 0; i < n; i++) {
        normalized[i] = alpha_normalize_atom(arena, parsed[i]);
        if (!normalized[i])
            return false;
    }
    if (n > 1)
        qsort(normalized, (size_t)n, sizeof(Atom *), qsort_atom_compare);
    free(parsed);
    *out_atoms = normalized;
    *out_len = n;
    return true;
}

static void print_atom_line(const char *prefix, Atom *atom) {
    fputs(prefix, stderr);
    atom_print(atom, stderr);
    fputc('\n', stderr);
}

static bool compare_outputs(const char *actual_label,
                            Atom **expected,
                            int expected_len,
                            Atom **actual,
                            int actual_len) {
    if (expected_len != actual_len) {
        fprintf(stderr,
                "cetta-oracle-diff: %s atom count differs: expected %d, got %d\n",
                actual_label, expected_len, actual_len);
        if (expected_len > 0)
            print_atom_line("first expected: ", expected[0]);
        if (actual_len > 0)
            print_atom_line("first actual:   ", actual[0]);
        return false;
    }
    for (int i = 0; i < expected_len; i++) {
        if (oracle_atom_compare(expected[i], actual[i]) == 0)
            continue;
        fprintf(stderr,
                "cetta-oracle-diff: %s differs at sorted atom %d\n",
                actual_label, i);
        print_atom_line("expected: ", expected[i]);
        print_atom_line("actual:   ", actual[i]);
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    const char *expected_file = NULL;
    const char *actual_file = NULL;
    const char *actual_text = NULL;
    const char *actual_label = "actual";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--expected-file") == 0 && i + 1 < argc) {
            expected_file = argv[++i];
        } else if (strcmp(argv[i], "--actual-file") == 0 && i + 1 < argc) {
            actual_file = argv[++i];
        } else if (strcmp(argv[i], "--actual-text") == 0 && i + 1 < argc) {
            actual_text = argv[++i];
        } else if (strcmp(argv[i], "--actual-label") == 0 && i + 1 < argc) {
            actual_label = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else {
            usage(stderr);
            return 2;
        }
    }

    if (!expected_file || (!!actual_file == !!actual_text)) {
        usage(stderr);
        return 2;
    }

    char *expected_raw = read_file_text(expected_file);
    if (!expected_raw) {
        fprintf(stderr, "cetta-oracle-diff: could not read expected file %s\n",
                expected_file);
        return 2;
    }
    char *actual_raw_owned = actual_file ? read_file_text(actual_file) : NULL;
    const char *actual_raw = actual_text;
    if (actual_file) {
        if (!actual_raw_owned) {
            fprintf(stderr, "cetta-oracle-diff: could not read actual file %s\n",
                    actual_file);
            free(expected_raw);
            return 2;
        }
        actual_raw = actual_raw_owned;
    }

    SymbolTable symbols;
    VarInternTable vars;
    HashConsTable hashcons;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    var_intern_init(&vars);
    g_var_intern = &vars;
    hashcons_init(&hashcons);
    g_hashcons = &hashcons;
    arena_init(&arena);
    arena_set_hashcons(&arena, &hashcons);
    cetta_language_set_current(cetta_language_lookup("he"));

    Atom **expected_atoms = NULL;
    Atom **actual_atoms = NULL;
    int expected_len = 0;
    int actual_len = 0;
    bool ok = parse_output_atoms("expected", expected_raw, &arena,
                                 &expected_atoms, &expected_len) &&
              parse_output_atoms(actual_label, actual_raw, &arena,
                                 &actual_atoms, &actual_len) &&
              compare_outputs(actual_label, expected_atoms, expected_len,
                              actual_atoms, actual_len);

    arena_free(&arena);
    hashcons_free(&hashcons);
    g_hashcons = NULL;
    var_intern_free(&vars);
    g_var_intern = NULL;
    symbol_table_free(&symbols);
    g_symbols = NULL;
    free(expected_raw);
    free(actual_raw_owned);
    return ok ? 0 : 1;
}
