#define _GNU_SOURCE
#include "atom.h"
#include "parser.h"
#include "space.h"
#include "eval.h"
#include "library.h"
#include "lang.h"
#include "lang_adapter.h"
#include "compile.h"
#include "cetta_stdlib.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "stats.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>

static char alt_stack_buf[16384];  /* alternate signal stack for SIGSEGV handler */

static void handle_sigsegv(int sig) {
    (void)sig;
    const char msg[] = "\nStack overflow. Use tail recursion or increase stack: ulimit -s unlimited\n";
    ssize_t r = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)r;
    _exit(2);
}

static bool g_count_only = false;
static bool g_quiet_results = false;
static const uint64_t CETTA_MM2_DEFAULT_RUN_STEPS = 1000000000000000ULL;

typedef enum {
    CETTA_DISPLAY_VARS_AUTO = 0,
    CETTA_DISPLAY_VARS_PRETTY,
    CETTA_DISPLAY_VARS_RAW
} CettaDisplayVarsMode;

typedef enum {
    CETTA_DISPLAY_NAMESPACES_AUTO = 0,
    CETTA_DISPLAY_NAMESPACES_PRETTY,
    CETTA_DISPLAY_NAMESPACES_RAW
} CettaDisplayNamespacesMode;

typedef struct {
    VarId var_id;
    const char *base_name;
    const char *display_name;
} CettaDisplayVarEntry;

typedef struct {
    CettaDisplayVarEntry *entries;
    uint32_t len;
    uint32_t cap;
} CettaDisplayVarMap;

static CettaDisplayVarsMode g_display_vars_mode = CETTA_DISPLAY_VARS_AUTO;
static CettaDisplayNamespacesMode g_display_namespaces_mode =
    CETTA_DISPLAY_NAMESPACES_AUTO;

static bool result_set_has_error(ResultSet *rs) {
    for (uint32_t i = 0; i < rs->len; i++) {
        if (atom_is_error(rs->items[i])) return true;
    }
    return false;
}

static bool result_set_all_empty(ResultSet *rs) {
    if (rs->len == 0) return false;
    for (uint32_t i = 0; i < rs->len; i++) {
        Atom *item = rs->items[i];
        if (!atom_is_empty(item) &&
            !(item->kind == ATOM_EXPR && item->expr.len == 0)) {
            return false;
        }
    }
    return true;
}

static bool path_has_suffix(const char *path, const char *suffix) {
    size_t path_len;
    size_t suffix_len;
    if (!path || !suffix) return false;
    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if (suffix_len > path_len) return false;
    return strcmp(path + (path_len - suffix_len), suffix) == 0;
}

static uint8_t *read_file_bytes(const char *path, size_t *len_out) {
    FILE *fp = NULL;
    long size = 0;
    size_t read_len = 0;
    uint8_t *buf = NULL;

    if (len_out) *len_out = 0;
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) goto fail;
    size = ftell(fp);
    if (size < 0) goto fail;
    if (fseek(fp, 0, SEEK_SET) != 0) goto fail;
    buf = malloc((size_t)size + 1u);
    if (!buf) goto fail;
    read_len = fread(buf, 1, (size_t)size, fp);
    if (read_len != (size_t)size) goto fail;
    buf[read_len] = '\0';
    if (fclose(fp) != 0) {
        fp = NULL;
        goto fail;
    }
    if (len_out) *len_out = read_len;
    return buf;

fail:
    if (fp) fclose(fp);
    free(buf);
    return NULL;
}

static void display_var_map_free(CettaDisplayVarMap *map) {
    free(map->entries);
    map->entries = NULL;
    map->len = 0;
    map->cap = 0;
}

static bool display_var_map_push(CettaDisplayVarMap *map, VarId var_id,
                                 const char *base_name) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->entries[i].var_id == var_id) {
            return true;
        }
    }
    if (map->len == map->cap) {
        uint32_t next_cap = map->cap ? map->cap * 2u : 16u;
        CettaDisplayVarEntry *next =
            realloc(map->entries, sizeof(CettaDisplayVarEntry) * next_cap);
        if (!next) {
            return false;
        }
        map->entries = next;
        map->cap = next_cap;
    }
    map->entries[map->len].var_id = var_id;
    map->entries[map->len].base_name = base_name;
    map->entries[map->len].display_name = NULL;
    map->len++;
    return true;
}

static bool display_var_collect_atom(CettaDisplayVarMap *map, Atom *atom) {
    if (!atom) return true;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return display_var_map_push(map, atom->var_id, atom_name_cstr(atom));
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_STATE) {
            StateCell *cell = (StateCell *)atom->ground.ptr;
            if (!cell) return true;
            if (!display_var_collect_atom(map, cell->value)) return false;
            return display_var_collect_atom(map, cell->content_type);
        }
        return true;
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (!display_var_collect_atom(map, atom->expr.elems[i])) return false;
        }
        return true;
    }
    return true;
}

static bool namespace_display_segment_start_char(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool namespace_display_segment_char(char c) {
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '!' || c == '?';
}

static bool namespace_display_looks_qualified(const char *name) {
    if (!name || !*name || !strchr(name, ':') || name[0] == ':') {
        return false;
    }

    bool at_segment_start = true;
    for (const char *p = name; *p; p++) {
        if (*p == ':') {
            if (at_segment_start || p[1] == '\0' || p[1] == ':') {
                return false;
            }
            at_segment_start = true;
            continue;
        }
        if (at_segment_start) {
            if (!namespace_display_segment_start_char(*p)) {
                return false;
            }
            at_segment_start = false;
            continue;
        }
        if (!namespace_display_segment_char(*p)) {
            return false;
        }
    }
    return !at_segment_start;
}

static const char *display_namespace_name(Arena *arena, const char *name,
                                          bool pretty_namespaces) {
    if (!pretty_namespaces || !name || !*name || !strchr(name, ':')) {
        return name;
    }
    if (!namespace_display_looks_qualified(name)) {
        return name;
    }

    size_t len = strlen(name);
    char *pretty = arena_alloc(arena, len + 1);
    if (!pretty) return name;
    for (size_t i = 0; i < len; i++) {
        pretty[i] = (name[i] == ':') ? '.' : name[i];
    }
    pretty[len] = '\0';
    return pretty;
}

static bool display_var_finalize(CettaDisplayVarMap *map, Arena *arena,
                                 bool pretty_namespaces) {
    for (uint32_t i = 0; i < map->len; i++) {
        const char *base = map->entries[i].base_name;
        const char *display_base =
            display_namespace_name(arena, base, pretty_namespaces);
        uint32_t group_size = 0;
        uint32_t rank = 0;
        for (uint32_t j = 0; j < map->len; j++) {
            if (strcmp(map->entries[j].base_name, base) != 0) continue;
            group_size++;
            if (map->entries[j].var_id < map->entries[i].var_id) {
                rank++;
            }
        }
        if (group_size <= 1 || rank == 0) {
            map->entries[i].display_name = display_base;
            continue;
        }
        char buf[256];
        snprintf(buf, sizeof(buf), "%s_%u", display_base, rank);
        map->entries[i].display_name = arena_strdup(arena, buf);
        if (!map->entries[i].display_name) {
            return false;
        }
    }
    return true;
}

static const char *display_var_lookup(const CettaDisplayVarMap *map, VarId var_id) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->entries[i].var_id == var_id) {
            return map->entries[i].display_name ?
                map->entries[i].display_name : map->entries[i].base_name;
        }
    }
    return NULL;
}

static Atom *display_atom_copy(Arena *dst, Atom *src, const CettaDisplayVarMap *map,
                               bool pretty_namespaces) {
    if (!src) return NULL;
    switch (src->kind) {
    case ATOM_SYMBOL: {
        const char *name =
            display_namespace_name(dst, atom_name_cstr(src), pretty_namespaces);
        return atom_symbol(dst, name);
    }
    case ATOM_VAR: {
        const char *name = display_var_lookup(map, src->var_id);
        if (!name) name = atom_name_cstr(src);
        name = display_namespace_name(dst, name, pretty_namespaces);
        return atom_var(dst, name);
    }
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT:
            return atom_int(dst, src->ground.ival);
        case GV_FLOAT:
            return atom_float(dst, src->ground.fval);
        case GV_BOOL:
            return atom_bool(dst, src->ground.bval);
        case GV_STRING:
            return atom_string(dst, src->ground.sval);
        case GV_SPACE:
            return atom_space(dst, src->ground.ptr);
        case GV_CAPTURE:
            return atom_capture(dst, (CaptureClosure *)src->ground.ptr);
        case GV_FOREIGN:
            return atom_foreign(dst, (CettaForeignValue *)src->ground.ptr);
        case GV_STATE: {
            StateCell *src_cell = (StateCell *)src->ground.ptr;
            StateCell *dst_cell = arena_alloc(dst, sizeof(StateCell));
            dst_cell->value = src_cell ?
                display_atom_copy(dst, src_cell->value, map, pretty_namespaces) : NULL;
            dst_cell->content_type =
                src_cell ? display_atom_copy(dst, src_cell->content_type, map,
                                             pretty_namespaces) : NULL;
            return atom_state(dst, dst_cell);
        }
        }
        break;
    case ATOM_EXPR: {
        Atom **elems = arena_alloc(dst, sizeof(Atom *) * src->expr.len);
        for (uint32_t i = 0; i < src->expr.len; i++) {
            elems[i] = display_atom_copy(dst, src->expr.elems[i], map,
                                         pretty_namespaces);
        }
        return atom_expr(dst, elems, src->expr.len);
    }
    }
    return atom_symbol(dst, "?");
}

static bool display_vars_pretty_enabled_for(FILE *logical_dest) {
    if (g_display_vars_mode == CETTA_DISPLAY_VARS_PRETTY) return true;
    if (g_display_vars_mode == CETTA_DISPLAY_VARS_RAW) return false;
    int fd = fileno(logical_dest);
    return fd >= 0 && isatty(fd);
}

static bool display_namespaces_pretty_enabled_for(FILE *logical_dest) {
    if (g_display_namespaces_mode == CETTA_DISPLAY_NAMESPACES_PRETTY) return true;
    if (g_display_namespaces_mode == CETTA_DISPLAY_NAMESPACES_RAW) return false;
    int fd = fileno(logical_dest);
    return fd >= 0 && isatty(fd);
}

static bool write_pretty_results(FILE *out, ResultSet *rs, bool pretty_vars,
                                 bool pretty_namespaces) {
    Arena pretty_arena;
    CettaDisplayVarMap map = {0};
    arena_init(&pretty_arena);
    arena_set_hashcons(&pretty_arena, NULL);

    if (pretty_vars) {
        for (uint32_t i = 0; i < rs->len; i++) {
            if (!display_var_collect_atom(&map, rs->items[i])) {
                display_var_map_free(&map);
                arena_free(&pretty_arena);
                return false;
            }
        }
        if (!display_var_finalize(&map, &pretty_arena, pretty_namespaces)) {
            display_var_map_free(&map);
            arena_free(&pretty_arena);
            return false;
        }
    }

    fprintf(out, "[");
    for (uint32_t i = 0; i < rs->len; i++) {
        Atom *pretty = display_atom_copy(&pretty_arena, rs->items[i], &map,
                                         pretty_namespaces);
        if (i > 0) fprintf(out, ", ");
        atom_print(pretty, out);
    }
    fprintf(out, "]\n");

    display_var_map_free(&map);
    arena_free(&pretty_arena);
    return true;
}

static int run_mm2_program_via_mork(Arena *arena, Atom **atoms, int n,
                                    bool count_only, uint64_t step_limit) {
    CettaMorkSpaceHandle *space = NULL;
    uint64_t ignored = 0;
    uint64_t size = 0;
    uint8_t *dump = NULL;
    size_t dump_len = 0;
    uint32_t dump_rows = 0;
    int rc = 0;

    if (!cetta_mork_bridge_is_available()) {
        fprintf(stderr, "error: MM2 runtime requires MORK bridge support: %s\n",
                cetta_mork_bridge_last_error());
        return 2;
    }

    space = cetta_mork_bridge_space_new();
    if (!space) {
        fprintf(stderr, "error: could not allocate MM2 space: %s\n",
                cetta_mork_bridge_last_error());
        return 1;
    }

    for (int i = 0; i < n; i++) {
        char *surface = cetta_mm2_atom_to_surface_string(arena, atoms[i]);
        bool ok = cetta_mork_bridge_space_add_text(space, surface, &ignored);
        if (!ok) {
            fprintf(stderr, "error: MM2 runtime could not load atom into live space: %s\n",
                    cetta_mork_bridge_last_error());
            rc = 1;
            goto done;
        }
    }

    if (!cetta_mork_bridge_space_step(space, step_limit, &ignored)) {
        fprintf(stderr, "error: MM2 runtime execution failed: %s\n",
                cetta_mork_bridge_last_error());
        rc = 1;
        goto done;
    }

    if (count_only) {
        if (!cetta_mork_bridge_space_size(space, &size)) {
            fprintf(stderr, "error: MM2 runtime could not measure final space: %s\n",
                    cetta_mork_bridge_last_error());
            rc = 1;
            goto done;
        }
        fprintf(stdout, "%llu\n", (unsigned long long)size);
        goto done;
    }

    if (!cetta_mork_bridge_space_dump(space, &dump, &dump_len, &dump_rows)) {
        fprintf(stderr, "error: MM2 runtime could not dump final space: %s\n",
                cetta_mork_bridge_last_error());
        rc = 1;
        goto done;
    }
    if (dump_len > 0 && fwrite(dump, 1, dump_len, stdout) != dump_len) {
        fprintf(stderr, "error: could not write MM2 runtime output\n");
        rc = 1;
        goto done;
    }
    (void)dump_rows;

done:
    cetta_mork_bridge_bytes_free(dump, dump_len);
    cetta_mork_bridge_space_free(space);
    return rc;
}

static int run_mm2_file_via_mork(const char *filepath, bool count_only,
                                 uint64_t step_limit) {
    CettaMorkSpaceHandle *space = NULL;
    uint8_t *text = NULL;
    size_t text_len = 0;
    uint64_t ignored = 0;
    uint64_t size = 0;
    uint8_t *dump = NULL;
    size_t dump_len = 0;
    uint32_t dump_rows = 0;
    int rc = 0;

    if (!cetta_mork_bridge_is_available()) {
        fprintf(stderr, "error: MM2 runtime requires MORK bridge support: %s\n",
                cetta_mork_bridge_last_error());
        return 2;
    }

    text = read_file_bytes(filepath, &text_len);
    if (!text) {
        fprintf(stderr, "error: could not read %s\n", filepath);
        return 1;
    }

    space = cetta_mork_bridge_space_new();
    if (!space) {
        fprintf(stderr, "error: could not allocate MM2 space: %s\n",
                cetta_mork_bridge_last_error());
        rc = 1;
        goto done;
    }

    if (!cetta_mork_bridge_space_add_sexpr(space, text, text_len, &ignored)) {
        fprintf(stderr, "error: MM2 runtime could not load raw file into live space: %s\n",
                cetta_mork_bridge_last_error());
        rc = 1;
        goto done;
    }

    if (!cetta_mork_bridge_space_step(space, step_limit, &ignored)) {
        fprintf(stderr, "error: MM2 runtime execution failed: %s\n",
                cetta_mork_bridge_last_error());
        rc = 1;
        goto done;
    }

    if (count_only) {
        if (!cetta_mork_bridge_space_size(space, &size)) {
            fprintf(stderr, "error: MM2 runtime could not measure final space: %s\n",
                    cetta_mork_bridge_last_error());
            rc = 1;
            goto done;
        }
        fprintf(stdout, "%llu\n", (unsigned long long)size);
        goto done;
    }

    if (!cetta_mork_bridge_space_dump(space, &dump, &dump_len, &dump_rows)) {
        fprintf(stderr, "error: MM2 runtime could not dump final space: %s\n",
                cetta_mork_bridge_last_error());
        rc = 1;
        goto done;
    }
    if (dump_len > 0 && fwrite(dump, 1, dump_len, stdout) != dump_len) {
        fprintf(stderr, "error: could not write MM2 runtime output\n");
        rc = 1;
        goto done;
    }
    (void)dump_rows;

done:
    cetta_mork_bridge_bytes_free(dump, dump_len);
    cetta_mork_bridge_space_free(space);
    free(text);
    return rc;
}

static void write_results(FILE *out, ResultSet *rs) {
    FILE *logical_dest = stdout;
    if (rs->len == 0) return;  /* HE prints nothing for empty result sets */
    if (g_count_only) {
        if (rs->len == 1 &&
            rs->items[0]->kind == ATOM_GROUNDED &&
            rs->items[0]->ground.gkind == GV_INT) {
            fprintf(out, "%lld\n", (long long)rs->items[0]->ground.ival);
            return;
        }
        fprintf(out, "%u\n", rs->len);
        return;
    }
    if (g_quiet_results && !result_set_has_error(rs) && result_set_all_empty(rs)) {
        return;
    }
    bool pretty_vars = display_vars_pretty_enabled_for(logical_dest);
    bool pretty_namespaces = display_namespaces_pretty_enabled_for(logical_dest);
    if (pretty_vars || pretty_namespaces) {
        if (write_pretty_results(out, rs, pretty_vars, pretty_namespaces)) {
            return;
        }
    }
    fprintf(out, "[");
    for (uint32_t i = 0; i < rs->len; i++) {
        if (i > 0) fprintf(out, ", ");
        atom_print(rs->items[i], out);
    }
    fprintf(out, "]\n");
}

static void print_usage(FILE *out) {
    fputs("usage: cetta [--lang <name>] <file.metta>\n", out);
    fputs("       cetta -e '<expr>' [-e '<expr>' ...]  # inline expressions (multiple -e concatenate)\n", out);
    fputs("       cetta [--profile <he_compat|he_extended|he_prime>] <file.metta>\n", out);
    fputs("       cetta [--import-mode <upstream|relative|ancestor-walk>] <file.metta>\n", out);
    fputs("       note: --lang selects the driver/front-end; --profile selects the visible surface policy\n", out);
    fputs("       cetta --help | -h                    # print this usage summary\n", out);
    fputs("       cetta --version | -v                 # print binary version and build mode\n", out);
    fputs("       cetta --compile <file.metta>           # emit LLVM IR to stdout\n", out);
    fputs("       cetta --compile-stdlib <file.metta>     # emit precompiled stdlib blob to stdout\n", out);
    fputs("       cetta --count-only <file.metta>        # print result counts only\n", out);
    fputs("       cetta --quiet <file.metta>              # hide pure [()] success clutter\n", out);
    fputs("       cetta --emit-runtime-stats <file.metta> # dump runtime counters to stderr after execution\n", out);
    fputs("       cetta --pretty-vars <file.metta>       # pretty-print result vars for humans\n", out);
    fputs("       cetta --raw-vars <file.metta>          # print raw internal var epochs\n", out);
    fputs("       cetta --pretty-namespaces <file.metta> # pretty-print mork./runtime. namespace sugar\n", out);
    fputs("       cetta --raw-namespaces <file.metta>    # print canonical mork:/runtime: names\n", out);
    fputs("       cetta --fuel <n> <file.metta>          # override evaluator fuel budget\n", out);
    fputs("       cetta --lang mm2 --steps <n> <file.mm2> # run at most n MM2 steps\n", out);
    fputs("       cetta --space-engine <name> <file.metta>\n", out);
    fputs("       cetta --list-profiles\n", out);
    fputs("       cetta --list-space-engines\n", out);
    fputs("       cetta --list-languages\n", out);
}

static void print_version(FILE *out) {
    fprintf(out, "cetta %s (%s)\n", CETTA_VERSION_STRING, CETTA_BUILD_MODE_STRING);
}

static const char *find_profile_blocked_surface(const CettaProfile *profile, Atom *atom) {
    if (!profile || !atom) return NULL;
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0) return NULL;

    Atom *head = atom->expr.elems[0];
    if (head->kind == ATOM_SYMBOL) {
        if (atom_is_symbol_id(head, g_builtin_syms.quote)) {
            return NULL;
        }
        if (!cetta_profile_allows_surface(profile, atom_name_cstr(head))) {
            return atom_name_cstr(head);
        }
        if (atom_is_symbol_id(head, g_builtin_syms.colon) && atom->expr.len >= 2 &&
            atom->expr.elems[1]->kind == ATOM_SYMBOL &&
            !cetta_profile_allows_surface(profile, atom_name_cstr(atom->expr.elems[1]))) {
            return atom_name_cstr(atom->expr.elems[1]);
        }
    }

    for (uint32_t i = 1; i < atom->expr.len; i++) {
        const char *blocked = find_profile_blocked_surface(profile, atom->expr.elems[i]);
        if (blocked) return blocked;
    }
    return NULL;
}

static bool compile_profile_guard_ok(const CettaProfile *profile, Atom **atoms, int n) {
    for (int i = 0; i < n; i++) {
        Atom *at = atoms[i];
        if (atom_is_symbol_id(at, g_builtin_syms.bang)) {
            i++;
            continue;
        }
        const char *blocked = find_profile_blocked_surface(profile, at);
        if (blocked) {
            const CettaSurfacePolicy *policy = cetta_surface_policy_lookup(blocked);
            fprintf(stderr, "error: surface '%s' is unavailable in profile '%s'",
                    blocked, profile ? profile->name : "unknown");
            if (policy && policy->surface_classification) {
                fprintf(stderr, " (%s)", policy->surface_classification);
            }
            fputc('\n', stderr);
            return false;
        }
    }
    return true;
}

static bool atom_id_is_symbol_id(const TermUniverse *universe, AtomId atom_id,
                                 SymbolId sym_id) {
    return universe && atom_id != CETTA_ATOM_ID_NONE &&
           tu_kind(universe, atom_id) == ATOM_SYMBOL &&
           tu_sym(universe, atom_id) == sym_id;
}

static bool compile_profile_guard_ok_ids(const CettaProfile *profile,
                                         TermUniverse *universe,
                                         const AtomId *atom_ids, int n) {
    if (!universe)
        return false;
    if (n <= 0)
        return true;
    if (!atom_ids)
        return false;
    for (int i = 0; i < n; i++) {
        AtomId atom_id = atom_ids[i];
        if (atom_id_is_symbol_id(universe, atom_id, g_builtin_syms.bang)) {
            i++;
            continue;
        }
        Atom *at = term_universe_get_atom(universe, atom_id);
        if (!at) {
            fprintf(stderr, "error: could not decode parsed term for compile guard\n");
            return false;
        }
        const char *blocked = find_profile_blocked_surface(profile, at);
        if (blocked) {
            const CettaSurfacePolicy *policy = cetta_surface_policy_lookup(blocked);
            fprintf(stderr, "error: surface '%s' is unavailable in profile '%s'",
                    blocked, profile ? profile->name : "unknown");
            if (policy && policy->surface_classification) {
                fprintf(stderr, " (%s)", policy->surface_classification);
            }
            fputc('\n', stderr);
            return false;
        }
    }
    return true;
}

static bool main_try_add_builtin_type_decls_direct(Space *space) {
    if (!space || !space->universe)
        return false;

    static const char *arith_ops[] = {"+", "-", "*", "/", "%", NULL};
    static const char *cmp_ops[] = {"<", ">", "<=", ">=", NULL};

    TermUniverse *universe = space->universe;
    AtomId decl_ids[12];
    uint32_t decl_count = 0;

    AtomId colon_id = tu_intern_symbol(universe, g_builtin_syms.colon);
    AtomId arrow_id = tu_intern_symbol(universe, g_builtin_syms.arrow);
    AtomId equals_id = tu_intern_symbol(universe, g_builtin_syms.equals);
    AtomId eqeq_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "=="));
    AtomId neq_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "!="));
    AtomId number_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "Number"));
    AtomId bool_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "Bool"));
    AtomId undefined_id =
        tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "%Undefined%"));
    SymbolId t_spelling = symbol_intern_cstr(g_symbols, "t");
    VarId t_var = g_var_intern ? var_intern(g_var_intern, t_spelling) : fresh_var_id();
    AtomId t_var_id = tu_intern_var(universe, t_spelling, t_var);
    AtomId arrow_nnn_children[4] = {arrow_id, number_id, number_id, number_id};
    AtomId arrow_nnb_children[4] = {arrow_id, number_id, number_id, bool_id};
    AtomId arrow_eq_children[4] = {arrow_id, t_var_id, t_var_id, undefined_id};
    AtomId arrow_eqeq_children[4] = {arrow_id, t_var_id, t_var_id, bool_id};

    if (colon_id == CETTA_ATOM_ID_NONE || arrow_id == CETTA_ATOM_ID_NONE ||
        equals_id == CETTA_ATOM_ID_NONE || eqeq_id == CETTA_ATOM_ID_NONE ||
        neq_id == CETTA_ATOM_ID_NONE ||
        number_id == CETTA_ATOM_ID_NONE || bool_id == CETTA_ATOM_ID_NONE ||
        undefined_id == CETTA_ATOM_ID_NONE || t_var_id == CETTA_ATOM_ID_NONE)
        return false;

    AtomId arrow_nnn_id = tu_expr_from_ids(universe, arrow_nnn_children, 4);
    AtomId arrow_nnb_id = tu_expr_from_ids(universe, arrow_nnb_children, 4);
    AtomId arrow_eq_id = tu_expr_from_ids(universe, arrow_eq_children, 4);
    AtomId arrow_eqeq_id = tu_expr_from_ids(universe, arrow_eqeq_children, 4);
    if (arrow_nnn_id == CETTA_ATOM_ID_NONE || arrow_nnb_id == CETTA_ATOM_ID_NONE ||
        arrow_eq_id == CETTA_ATOM_ID_NONE || arrow_eqeq_id == CETTA_ATOM_ID_NONE)
        return false;

    for (const char **op = arith_ops; *op; op++) {
        AtomId op_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, *op));
        AtomId decl_children[3] = {colon_id, op_id, arrow_nnn_id};
        if (op_id == CETTA_ATOM_ID_NONE)
            return false;
        decl_ids[decl_count] = tu_expr_from_ids(universe, decl_children, 3);
        if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
            return false;
    }
    for (const char **op = cmp_ops; *op; op++) {
        AtomId op_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, *op));
        AtomId decl_children[3] = {colon_id, op_id, arrow_nnb_id};
        if (op_id == CETTA_ATOM_ID_NONE)
            return false;
        decl_ids[decl_count] = tu_expr_from_ids(universe, decl_children, 3);
        if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
            return false;
    }

    AtomId eq_decl_children[3] = {colon_id, equals_id, arrow_eq_id};
    decl_ids[decl_count] = tu_expr_from_ids(universe, eq_decl_children, 3);
    if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
        return false;

    AtomId eqeq_decl_children[3] = {colon_id, eqeq_id, arrow_eqeq_id};
    decl_ids[decl_count] = tu_expr_from_ids(universe, eqeq_decl_children, 3);
    if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
        return false;
    AtomId neq_decl_children[3] = {colon_id, neq_id, arrow_eqeq_id};
    decl_ids[decl_count] = tu_expr_from_ids(universe, neq_decl_children, 3);
    if (decl_ids[decl_count++] == CETTA_ATOM_ID_NONE)
        return false;

    for (uint32_t i = 0; i < decl_count; i++)
        space_add_atom_id(space, decl_ids[i]);
    return true;
}

static void main_add_builtin_type_decls(Space *space, Arena *arena) {
    if (main_try_add_builtin_type_decls_direct(space))
        return;

    Atom *num = atom_symbol(arena, "Number");
    Atom *arrow_nnn = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), num, num, num}, 4);
    Atom *bool_t = atom_symbol(arena, "Bool");
    Atom *arrow_nnb = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), num, num, bool_t}, 4);
    const char *arith_ops[] = {"+", "-", "*", "/", "%", NULL};
    const char *cmp_ops[] = {"<", ">", "<=", ">=", NULL};
    for (const char **op = arith_ops; *op; op++) {
        Atom *decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, *op), arrow_nnn);
        if (!space_admit_atom(space, arena, decl))
            space_add(space, decl);
    }
    for (const char **op = cmp_ops; *op; op++) {
        Atom *decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, *op), arrow_nnb);
        if (!space_admit_atom(space, arena, decl))
            space_add(space, decl);
    }
    Atom *t_var = atom_var(arena, "t");
    Atom *arrow_eq = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), t_var, t_var,
        atom_undefined_type(arena)}, 4);
    Atom *eq_decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
        atom_symbol_id(arena, g_builtin_syms.equals), arrow_eq);
    if (!space_admit_atom(space, arena, eq_decl))
        space_add(space, eq_decl);
    Atom *arrow_eqeq = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), t_var, t_var, bool_t}, 4);
    Atom *eqeq_decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
        atom_symbol(arena, "=="), arrow_eqeq);
    if (!space_admit_atom(space, arena, eqeq_decl))
        space_add(space, eqeq_decl);
    Atom *neq_decl = atom_expr3(arena, atom_symbol_id(arena, g_builtin_syms.colon),
        atom_symbol(arena, "!="), arrow_eqeq);
    if (!space_admit_atom(space, arena, neq_decl))
        space_add(space, neq_decl);
}

static void main_apply_language_stdlib_type_prunes(Space *space, Arena *arena,
                                                   const CettaLanguageSpec *lang) {
    size_t prune_count = 0;
    const CettaTypeDeclSignature *prunes =
        cetta_language_stdlib_type_prunes(lang, &prune_count);
    if (!prunes || prune_count == 0)
        return;

    for (size_t i = 0; i < prune_count; i++) {
        Atom *decl =
            atom_expr3(arena,
                       atom_symbol_id(arena, g_builtin_syms.colon),
                       atom_symbol(arena, prunes[i].head_name),
                       atom_expr(arena, (Atom *[]){
                           atom_symbol_id(arena, g_builtin_syms.arrow),
                           atom_symbol(arena, prunes[i].arg_type_name),
                           atom_symbol(arena, prunes[i].result_type_name)
                       }, 3));
        (void)space_remove(space, decl);
    }
}

static bool main_load_language_prelude(Space *space, Arena *arena,
                                       TermUniverse *universe,
                                       const CettaLanguageSpec *lang) {
    const char *prelude = cetta_language_prelude_text(lang);
    if (!prelude || prelude[0] == '\0')
        return true;

    AtomId *prelude_ids = NULL;
    int prelude_count =
        cetta_language_parse_text_ids(lang, prelude, arena, universe, &prelude_ids);
    if (prelude_count < 0)
        return false;
    for (int i = 0; i < prelude_count; i++)
        space_add_atom_id(space, prelude_ids[i]);
    return true;
}

typedef struct {
    char *inline_buf;
    Atom **atoms;
    AtomId *atom_ids;
    FILE *output_spool;
    Arena *arena;
    bool arena_initialized;
    Arena *eval_arena;
    bool eval_arena_initialized;
    SymbolTable *symbol_table;
    bool symbol_table_initialized;
    VarInternTable *var_intern_table;
    bool var_intern_initialized;
    HashConsTable *hashcons_table;
    bool hashcons_initialized;
    CettaLibraryContext *libraries;
    bool libraries_initialized;
    Space *space;
    bool space_initialized;
    Registry *registry;
    bool registry_initialized;
} CettaMainCleanup;

static void cetta_main_cleanup_registry_spaces(Registry *registry, Space *root_space) {
    if (!registry || !root_space) return;
    for (uint32_t ri = 0; ri < registry->len; ri++) {
        Atom *val = registry->entries[ri].value;
        if (!val) continue;
        if (val->kind == ATOM_GROUNDED && val->ground.gkind == GV_SPACE) {
            Space *sp = (Space *)val->ground.ptr;
            if (sp != root_space) {
                space_free(sp);
            }
        }
    }
}

static void cetta_main_cleanup(CettaMainCleanup *cleanup) {
    if (!cleanup) return;

    if (cleanup->output_spool) {
        fclose(cleanup->output_spool);
        cleanup->output_spool = NULL;
    }

    free(cleanup->atoms);
    cleanup->atoms = NULL;
    free(cleanup->atom_ids);
    cleanup->atom_ids = NULL;

    if (cleanup->registry_initialized && cleanup->space_initialized) {
        cetta_main_cleanup_registry_spaces(cleanup->registry, cleanup->space);
        cleanup->registry_initialized = false;
    }

    if (cleanup->libraries_initialized) {
        cetta_library_context_free(cleanup->libraries);
        cleanup->libraries_initialized = false;
    }

    if (cleanup->space_initialized) {
        space_free(cleanup->space);
        cleanup->space_initialized = false;
    }

    free(cleanup->inline_buf);
    cleanup->inline_buf = NULL;

    if (cleanup->eval_arena_initialized) {
        arena_free(cleanup->eval_arena);
        cleanup->eval_arena_initialized = false;
    }

    if (cleanup->arena_initialized) {
        arena_free(cleanup->arena);
        cleanup->arena_initialized = false;
    }

    if (cleanup->var_intern_initialized) {
        g_var_intern = NULL;
        var_intern_free(cleanup->var_intern_table);
        cleanup->var_intern_initialized = false;
    }

    if (cleanup->symbol_table_initialized) {
        g_symbols = NULL;
        symbol_table_free(cleanup->symbol_table);
        cleanup->symbol_table_initialized = false;
    }

    if (cleanup->hashcons_initialized) {
        g_hashcons = NULL;
        hashcons_free(cleanup->hashcons_table);
        cleanup->hashcons_initialized = false;
    }
}

int main(int argc, char **argv) {
    /* Install SIGSEGV handler on alternate stack so it works during stack overflow */
    {
        stack_t ss;
        ss.ss_sp = alt_stack_buf;
        ss.ss_size = sizeof(alt_stack_buf);
        ss.ss_flags = 0;
        sigaltstack(&ss, NULL);

        struct sigaction sa;
        sa.sa_handler = handle_sigsegv;
        sa.sa_flags = SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, NULL);
    }

    const char *lang_name = "he";
    const CettaProfile *profile = cetta_profile_he_extended();
    const char *filename = NULL;
    const char *inline_text = NULL;
    char *inline_buf = NULL;
    size_t inline_len = 0;
    size_t inline_cap = 0;
    const char *script_path = NULL;
    int script_arg_start = -1;
    bool import_mode_override = false;
    CettaRelativeModulePolicy import_mode =
        CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY;
    bool compile_mode = false;
    bool compile_stdlib_mode = false;
    bool count_only = false;
    bool emit_runtime_stats = false;
    int fuel_override = -1;
    uint64_t mm2_step_limit = CETTA_MM2_DEFAULT_RUN_STEPS;
    SpaceEngine space_engine = SPACE_ENGINE_NATIVE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_version(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--list-languages") == 0) {
            cetta_language_print_inventory(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--list-space-engines") == 0 ||
            strcmp(argv[i], "--list-space-match-backends") == 0) {
            space_match_backend_print_inventory(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--list-profiles") == 0) {
            cetta_profile_print_inventory(stdout);
            return 0;
        }
        if (strcmp(argv[i], "--compile") == 0) {
            compile_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--compile-stdlib") == 0) {
            compile_stdlib_mode = true;
            continue;
        }
        if (strcmp(argv[i], "--count-only") == 0) {
            count_only = true;
            continue;
        }
        if (strcmp(argv[i], "--quiet") == 0) {
            g_quiet_results = true;
            continue;
        }
        if (strcmp(argv[i], "--emit-runtime-stats") == 0) {
            emit_runtime_stats = true;
            continue;
        }
        if (strcmp(argv[i], "--pretty-vars") == 0) {
            g_display_vars_mode = CETTA_DISPLAY_VARS_PRETTY;
            continue;
        }
        if (strcmp(argv[i], "--raw-vars") == 0) {
            g_display_vars_mode = CETTA_DISPLAY_VARS_RAW;
            continue;
        }
        if (strcmp(argv[i], "--pretty-namespaces") == 0) {
            g_display_namespaces_mode = CETTA_DISPLAY_NAMESPACES_PRETTY;
            continue;
        }
        if (strcmp(argv[i], "--raw-namespaces") == 0) {
            g_display_namespaces_mode = CETTA_DISPLAY_NAMESPACES_RAW;
            continue;
        }
        if (strcmp(argv[i], "--fuel") == 0) {
            char *endp = NULL;
            long parsed;
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            parsed = strtol(argv[++i], &endp, 10);
            if (!endp || *endp != '\0' || parsed <= 0 || parsed > 100000000L) {
                fprintf(stderr, "error: invalid fuel '%s'\n", argv[i]);
                return 2;
            }
            fuel_override = (int)parsed;
            continue;
        }
        if (strcmp(argv[i], "--steps") == 0) {
            char *endp = NULL;
            unsigned long long parsed;
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            parsed = strtoull(argv[++i], &endp, 10);
            if (!endp || *endp != '\0') {
                fprintf(stderr, "error: invalid MM2 step count '%s'\n", argv[i]);
                return 2;
            }
            mm2_step_limit = (uint64_t)parsed;
            continue;
        }
        if (strcmp(argv[i], "--space-engine") == 0 ||
            strcmp(argv[i], "--space-match-backend") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            if (!space_match_backend_kind_from_name(argv[++i], &space_engine)) {
                fprintf(stderr, "error: unknown space engine '%s'\n", argv[i]);
                space_match_backend_print_inventory(stderr);
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "--lang") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            lang_name = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            profile = cetta_profile_from_name(argv[++i]);
            if (!profile) {
                fprintf(stderr, "error: unknown profile '%s'\n", argv[i]);
                cetta_profile_print_inventory(stderr);
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "--import-mode") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            if (!cetta_relative_module_policy_from_name(argv[++i], &import_mode)) {
                fprintf(stderr, "error: unknown import mode '%s'\n", argv[i]);
                return 2;
            }
            import_mode_override = true;
            continue;
        }
        if (strcmp(argv[i], "-e") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            const char *arg = argv[++i];
            size_t arg_len = strlen(arg);
            size_t needed = inline_len + (inline_len > 0 ? 1 : 0) + arg_len + 1;
            if (needed > inline_cap) {
                inline_cap = needed > inline_cap * 2 ? needed : inline_cap * 2;
                inline_buf = realloc(inline_buf, inline_cap);
            }
            if (inline_len > 0) inline_buf[inline_len++] = ' ';
            memcpy(inline_buf + inline_len, arg, arg_len);
            inline_len += arg_len;
            inline_buf[inline_len] = '\0';
            script_path = "<expr>";
            script_arg_start = i + 1;
            continue;
        }
        if (!filename) {
            filename = argv[i];
            script_path = filename;
            script_arg_start = i + 1;
            break;
        }
    }

    if (inline_buf)
        inline_text = inline_buf;
    if (!filename && !inline_text) {
        print_usage(stderr);
        free(inline_buf);
        return 1;
    }

    /* --compile-stdlib: parse .metta file, emit C blob header, exit */
    if (compile_stdlib_mode) {
        Arena tmp_arena;
        arena_init(&tmp_arena);
        SymbolTable tmp_symbols;
        symbol_table_init(&tmp_symbols);
        symbol_table_init_builtins(&tmp_symbols, &g_builtin_syms);
        VarInternTable tmp_var_intern;
        var_intern_init(&tmp_var_intern);
        g_symbols = &tmp_symbols;
        g_var_intern = &tmp_var_intern;
        int rc = stdlib_compile(filename, &tmp_arena, stdout);
        arena_free(&tmp_arena);
        g_symbols = NULL;
        g_var_intern = NULL;
        var_intern_free(&tmp_var_intern);
        symbol_table_free(&tmp_symbols);
        return rc < 0 ? 1 : 0;
    }

    const CettaLanguageSpec *lang = cetta_language_lookup(lang_name);
    if (!lang) {
        fprintf(stderr, "error: unknown language '%s'\n", lang_name);
        cetta_language_print_inventory(stderr);
        return 2;
    }
    if (!lang->implemented) {
        fprintf(
            stderr,
            "error: language '%s' is recognized but not implemented in cetta yet\n",
            lang_name
        );
        fprintf(stderr, "note: %s\n", lang->note);
        return 2;
    }
    if (strcmp(lang->canonical, "mm2") != 0 &&
        mm2_step_limit != CETTA_MM2_DEFAULT_RUN_STEPS) {
        fprintf(stderr, "error: --steps is currently only supported with --lang mm2\n");
        return 2;
    }

    if (!compile_mode && strcmp(lang->canonical, "mm2") == 0 &&
        filename && !inline_text && path_has_suffix(filename, ".mm2")) {
        int mm2_rc = run_mm2_file_via_mork(filename, count_only, mm2_step_limit);
        free(inline_buf);
        return mm2_rc;
    }

    int rc = 0;
    Arena arena;       /* persistent: parsed atoms, space content, type decls */
    Arena eval_arena;  /* ephemeral: intermediate eval results, reset per ! */
    SymbolTable symbol_table;
    VarInternTable var_intern_table;
    HashConsTable hashcons_table;
    Atom **atoms = NULL;
    AtomId *atom_ids = NULL;
    CettaLibraryContext libraries;
    Space space;
    Registry registry;
    CettaMainCleanup cleanup = {0};
    bool lang_is_mm2 = strcmp(lang->canonical, "mm2") == 0;
    int n = 0;

    g_count_only = count_only;
    if (fuel_override > 0) eval_set_default_fuel(fuel_override);

    cleanup.inline_buf = inline_buf;
    cleanup.arena = &arena;
    cleanup.eval_arena = &eval_arena;
    cleanup.symbol_table = &symbol_table;
    cleanup.var_intern_table = &var_intern_table;
    cleanup.hashcons_table = &hashcons_table;
    cleanup.libraries = &libraries;
    cleanup.space = &space;
    cleanup.registry = &registry;

    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    cleanup.arena_initialized = true;
    arena_init(&eval_arena);
    arena_set_runtime_kind(&eval_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
    cleanup.eval_arena_initialized = true;

    /* Global symbol table / builtin ids */
    symbol_table_init(&symbol_table);
    symbol_table_init_builtins(&symbol_table, &g_builtin_syms);
    cleanup.symbol_table_initialized = true;
    g_symbols = &symbol_table;
    var_intern_init(&var_intern_table);
    cleanup.var_intern_initialized = true;
    g_var_intern = &var_intern_table;

    /* Hash-consing for structural sharing (reduces memory for large derivations) */
    hashcons_init(&hashcons_table);
    cleanup.hashcons_initialized = true;
    g_hashcons = &hashcons_table;
    arena_set_hashcons(&arena, &hashcons_table);
    arena_set_hashcons(&eval_arena, NULL);

    if (lang_is_mm2) {
        n = inline_text
            ? parse_metta_text(inline_text, &arena, &atoms)
            : parse_metta_file(filename, &arena, &atoms);
        cleanup.atoms = atoms;
        if (n < 0) {
            if (inline_text) {
                fprintf(stderr, "error: could not parse inline MeTTa text\n");
            } else {
                fprintf(stderr, "error: could not read %s\n", filename);
            }
            rc = 1;
            goto cleanup;
        }
        cetta_mm2_lower_atoms(&arena, atoms, n);
    }
    if (emit_runtime_stats) {
        cetta_runtime_stats_reset();
        cetta_runtime_stats_enable();
    }
    if (lang_is_mm2 &&
        !cetta_mm2_atoms_have_top_level_eval(atoms, n)) {
        int mm2_rc = run_mm2_program_via_mork(&arena, atoms, n, count_only,
                                              mm2_step_limit);
        if (emit_runtime_stats) {
            CettaRuntimeStats stats;
            cetta_runtime_stats_snapshot(&stats);
            cetta_runtime_stats_print(stderr, &stats);
        }
        rc = mm2_rc;
        goto cleanup;
    }

    cetta_library_context_init_with_profile(&libraries, profile, lang);
    cleanup.libraries_initialized = true;
    term_universe_set_persistent_arena(&libraries.term_universe, &arena);
    cetta_library_context_set_exec_path(&libraries, argv[0]);
    cetta_library_context_set_script_path(&libraries, script_path);
    cetta_library_context_set_cli_args(&libraries, argc, argv, script_arg_start);
    if (import_mode_override) {
        cetta_eval_session_set_relative_module_policy(&libraries.session,
                                                      import_mode);
    }
    eval_set_library_context(&libraries);

    space_init_with_universe(&space, &libraries.term_universe);
    cleanup.space_initialized = true;
    if (!space_match_backend_try_set(&space, space_engine)) {
        const char *reason = space_match_backend_unavailable_reason(space_engine);
        if (reason) {
            fprintf(stderr, "error: %s\n", reason);
        } else {
            fprintf(stderr, "error: space engine '%s' is recognized but not implemented yet\n",
                    space_match_backend_kind_name(space_engine));
        }
        rc = 2;
        goto cleanup;
    }

    registry_init(&registry);
    cleanup.registry_initialized = true;
    registry_bind_id(&registry, g_builtin_syms.self, atom_space(&arena, &space));

    if (!lang_is_mm2) {
        n = inline_text
            ? cetta_language_parse_text_ids_for_session(&libraries.session,
                                                        inline_text, &arena,
                                                        &libraries.term_universe,
                                                        &atom_ids)
            : cetta_language_parse_file_ids_for_session(&libraries.session,
                                                        filename, &arena,
                                                        &libraries.term_universe,
                                                        &atom_ids);
        cleanup.atom_ids = atom_ids;
        if (n < 0) {
            if (inline_text) {
                fprintf(stderr, "error: could not parse inline MeTTa text\n");
            } else {
                fprintf(stderr, "error: could not read %s\n", filename);
            }
            rc = 1;
            goto cleanup;
        }
    }

    /* Load precompiled stdlib equations into the space */
    stdlib_load(&space, &arena);
    main_apply_language_stdlib_type_prunes(&space, &arena, lang);

    /* Add grounded op type declarations (HE stdlib implicit types) */
    if (cetta_language_injects_builtin_type_decls(lang))
        main_add_builtin_type_decls(&space, &arena);
    if (!main_load_language_prelude(&space, &arena, &libraries.term_universe, lang)) {
        fprintf(stderr, "error: could not load language prelude for %s\n",
                lang ? lang->canonical : "he");
        rc = 1;
        goto cleanup;
    }

    /* Compile mode: load all atoms into space, emit LLVM IR, exit */
    if (compile_mode) {
        if (lang_is_mm2) {
            if (!compile_profile_guard_ok(profile, atoms, n)) {
                rc = 2;
                goto cleanup;
            }
            for (int pi = 0; pi < n; pi++) {
                Atom *at = atoms[pi];
                if (atom_is_symbol_id(at, g_builtin_syms.bang)) {
                    pi++;
                    continue;
                }
                /* MM2 stays on the legacy mutable-Atom lane; 1f only makes the
                   MeTTa/HE persistent ingress born-canonical. */
                space_add(&space, at);
            }
        } else {
            if (!compile_profile_guard_ok_ids(profile, &libraries.term_universe,
                                              atom_ids, n)) {
                rc = 2;
                goto cleanup;
            }
            for (int pi = 0; pi < n; pi++) {
                if (atom_id_is_symbol_id(&libraries.term_universe, atom_ids[pi],
                                         g_builtin_syms.bang)) {
                    pi++;
                    continue;
                }
                space_add_atom_id(&space, atom_ids[pi]);
            }
        }
        compile_space_to_llvm(&space, &arena, stdout);
        rc = 0;
        goto cleanup;
    }

    /* Process top-level atoms */
    int i = 0;
    FILE *output_spool = tmpfile();
    if (!output_spool) {
        fprintf(stderr, "error: could not create output spool\n");
        rc = 1;
        goto cleanup;
    }
    cleanup.output_spool = output_spool;
    while (i < n) {
        if (lang_is_mm2) {
            Atom *at = atoms[i];

            /* ! prefix → evaluate and print */
            if (atom_is_symbol_id(at, g_builtin_syms.bang) && i + 1 < n) {
                Atom *expr = atoms[i + 1];
                ResultSet rs;
                result_set_init(&rs);
                eval_top_with_registry(&space, &eval_arena, &arena, &registry, expr, &rs);
                write_results(output_spool, &rs);
                if (fflush(output_spool) != 0) {
                    fprintf(stderr, "error: could not write output spool\n");
                    free(rs.items);
                    rc = 1;
                    goto cleanup;
                }
                bool stop_after_error = result_set_has_error(&rs);
                free(rs.items);
                eval_release_temporary_spaces();
                /* Reset ephemeral arena — frees all intermediate eval atoms.
                   This makes CeTTa safe for unlimited chaining iterations. */
                arena_free(&eval_arena);
                arena_init(&eval_arena);
                arena_set_runtime_kind(&eval_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
                arena_set_hashcons(&eval_arena, NULL);
                if (stop_after_error) break;
                i += 2;
                continue;
            }

            /* Otherwise: add to space */
            /* MM2 lowering still owns this mutable surface. */
            space_add(&space, at);
            i++;
            continue;
        }

        AtomId at_id = atom_ids[i];

        /* ! prefix → evaluate and print */
        if (atom_id_is_symbol_id(&libraries.term_universe, at_id,
                                 g_builtin_syms.bang) &&
            i + 1 < n) {
            Atom *expr = term_universe_copy_atom(&libraries.term_universe, &arena,
                                                 atom_ids[i + 1]);
            ResultSet rs;
            if (!expr) {
                fprintf(stderr, "error: could not decode top-level eval form\n");
                rc = 1;
                goto cleanup;
            }
            result_set_init(&rs);
            eval_top_with_registry(&space, &eval_arena, &arena, &registry, expr, &rs);
            write_results(output_spool, &rs);
            if (fflush(output_spool) != 0) {
                fprintf(stderr, "error: could not write output spool\n");
                free(rs.items);
                rc = 1;
                goto cleanup;
            }
            bool stop_after_error = result_set_has_error(&rs);
            free(rs.items);
            eval_release_temporary_spaces();
            arena_free(&eval_arena);
            arena_init(&eval_arena);
            arena_set_runtime_kind(&eval_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
            arena_set_hashcons(&eval_arena, NULL);
            if (stop_after_error) break;
            i += 2;
            continue;
        }

        space_add_atom_id(&space, at_id);
        i++;
    }

    if (fseek(output_spool, 0, SEEK_SET) != 0) {
        fprintf(stderr, "error: could not rewind output spool\n");
        rc = 1;
        goto cleanup;
    }
    {
        char io_buf[8192];
        size_t nread;
        while ((nread = fread(io_buf, 1, sizeof(io_buf), output_spool)) > 0) {
            if (fwrite(io_buf, 1, nread, stdout) != nread) {
                fprintf(stderr, "error: could not flush output spool to stdout\n");
                rc = 1;
                goto cleanup;
            }
        }
    }

    if (emit_runtime_stats) {
        CettaRuntimeStats stats;
        cetta_runtime_stats_snapshot(&stats);
        cetta_runtime_stats_print(stderr, &stats);
    }

    rc = 0;

cleanup:
    cetta_main_cleanup(&cleanup);
    return rc;
}
