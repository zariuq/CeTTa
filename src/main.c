#define _GNU_SOURCE
#include "atom.h"
#include "parser.h"
#include "he_compiled_reader.h"
#include "gslt_language_runtime.h"
#include "gslt_abt_provider_v1.h"
#include "gslt_revisioned_space_provider_v1.h"
#include "gslt_support_transform_runtime.h"
#include "generated/gslt_il_language_v1.generated.h"
#include "generated/metta_interact_language_v1.generated.h"
#include "generated/mm2_gslt_profile_v1.generated.h"
#include "generated/subzero_language_v1.generated.h"
#include "generated/zero_language_v1.generated.h"
#include "generated/zero_exp_language_v1.generated.h"
#include "generated/zero_emit_language_v1.generated.h"
#include "generated/zero_interact_language_v1.generated.h"
#include "generated/zero_interact_provider_catalog_v1.generated.h"
#include "generated/zerouv_language_v1.generated.h"
#include "petta_compiled_reader.h"
#include "petta_typecheck.h"
#include "lib_prolog.h"
#include "prime_compiled_reader.h"
#include "space.h"
#include "eval.h"
#include "library.h"
#include "lang.h"
#include "compile.h"
#include "cetta_stdlib.h"
#include "foreign.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "rhocalc_core.h"
#include "rhocalc_syntax.h"
#include "stats.h"
#include "native/langdef_module.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>

static char alt_stack_buf[16384];  /* alternate signal stack for SIGSEGV handler */

static void handle_sigsegv(int sig) {
    (void)sig;
    const char msg[] = "\nStack overflow. Use tail recursion or increase the stack limit.\n";
    ssize_t r = write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)r;
    _exit(2);
}

static bool g_count_only = false;
static bool g_quiet_results = false;
static uint64_t g_prime_need_trace_form = 0u;
static const uint64_t CETTA_MM2_DEFAULT_RUN_STEPS = 1000000000000000ULL;
static const uint32_t CETTA_RHOCALC_DEFAULT_REDUCTION_LIMIT = 100000u;
static const int CETTA_MM2_GSLT_EXIT_EXPIRED = 3;
static const int CETTA_RHOCALC_EXIT_REDUCTION_LIMIT_EXHAUSTED = 3;

static const CettaGsltEmbeddedLanguageV1 *
embedded_gslt_descriptor(CettaLanguageId language_id,
                         const CettaProfile *profile) {
    switch (language_id) {
    case CETTA_LANGUAGE_SUBZERO:
        return &cetta_subzero_language_v1;
    case CETTA_LANGUAGE_ZERO:
        if (profile && profile->id == CETTA_PROFILE_ZERO_EXP)
            return &cetta_zero_exp_language_v1;
        if (profile && profile->id == CETTA_PROFILE_ZERO_EMIT)
            return &cetta_zero_emit_language_v1;
        if (profile && profile->id == CETTA_PROFILE_ZERO_INTERACT)
            return &cetta_zero_interact_language_v1;
        return &cetta_zero_language_v1;
    case CETTA_LANGUAGE_GSLT_IL:
        return &cetta_gslt_il_language_v1;
    case CETTA_LANGUAGE_ZEROUV:
        return &cetta_zerouv_language_v1;
    case CETTA_LANGUAGE_METTA_INTERACT:
        return &cetta_metta_interact_language_v1;
    default:
        return NULL;
    }
}

static const CettaGsltRevisionedSpaceSchemaV1
    CETTA_ZERO_INTERACT_SPACE_SCHEMA_V1 = {
        .open_relation = "zero-space-open",
        .open_semantic_id = "zero.revisioned-space.open.v1",
        .member_relation = "zero-space-member",
        .member_semantic_id = "zero.revisioned-space.member.v1",
        .candidate_relation = "zero-space-candidate",
        .candidate_semantic_id = "zero.revisioned-space.candidate.v1",
        .emit_relation = "zero-space-emit",
        .emit_semantic_id = "zero.revisioned-space.emit.v1",
        .program_nil_constructor = "zero-program-nil",
        .program_cons_constructor = "zero-program-cons",
        .world_token_constructor = "zero-world-token",
        .stored_occurrence_constructor = "zero-space-stored-occurrence",
        .emitted_occurrence_constructor = "zero-space-emitted-occurrence",
        .open_receipt_constructor = "zero-space-open-receipt",
        .emit_receipt_constructor = "zero-space-emit-receipt",
};

static const CettaGsltAbtProviderSchemaV1
    CETTA_ZERO_INTERACT_ABT_SCHEMA_V1 = {
        .field_depth_relation = "qabt-field-depth",
        .field_depth_semantic_id = "abt.default-signature.field-depth.v1",
        .transport_relation = "qabt-transport",
        .transport_semantic_id = "abt.default-signature.transport.v1",
};

typedef enum {
    CETTA_MM2_GSLT_REALIZATION_AUTO = 0,
    CETTA_MM2_GSLT_REALIZATION_NATIVE_C,
    CETTA_MM2_GSLT_REALIZATION_RUST_PATHMAP_ABI,
} CettaMm2GsltRealization;

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
    const char *preferred_name;
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

static bool result_set_petta_typecheck_error(
    ResultSet *results, int *exit_code, const char **diagnostic) {
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
    if (!results)
        return false;
    for (uint32_t index = 0u; index < results->len; index++) {
        if (petta_typecheck_error_view(
                results->items[index], exit_code, diagnostic)) {
            return true;
        }
    }
    return false;
#else
    (void)results;
    (void)exit_code;
    (void)diagnostic;
    return false;
#endif
}

static bool result_set_all_empty(ResultSet *rs,
                                 CettaLanguageId language_id) {
    if (rs->len == 0) return false;
    for (uint32_t i = 0; i < rs->len; i++) {
        Atom *item = rs->items[i];
        if (!(language_id != CETTA_LANGUAGE_PRIME &&
              !cetta_language_uses_embedded_gslt(language_id) &&
              atom_is_empty(item)) &&
            !(item->kind == ATOM_EXPR && item->expr.len == 0)) {
            return false;
        }
    }
    return true;
}

static bool result_set_all_rhocalc_domain(ResultSet *rs) {
    if (rs->len == 0) return false;
    for (uint32_t i = 0; i < rs->len; i++) {
        if (!rhocalc_is_domain_atom(rs->items[i])) return false;
    }
    return true;
}

typedef struct {
    uint64_t raw;
    uint64_t local;
} PrimeNeedTraceId;

typedef struct {
    uint64_t session;
    uint64_t thunk;
    uint64_t local;
} PrimeNeedTraceCell;

typedef struct {
    StateCell *raw;
    uint64_t local;
} PrimeNeedTraceState;

typedef struct {
    FILE *out;
    uint64_t form;
    uint64_t answer;
    PrimeNeedTraceId *events;
    size_t events_len;
    PrimeNeedTraceId *sessions;
    size_t sessions_len;
    PrimeNeedTraceId *sources;
    size_t sources_len;
    PrimeNeedTraceId *rules;
    size_t rules_len;
    PrimeNeedTraceId *receipts;
    size_t receipts_len;
    PrimeNeedTraceCell *cells;
    size_t cells_len;
    PrimeNeedTraceState *states;
    size_t states_len;
    bool allocation_failed;
} PrimeNeedTracePrinter;

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static uint64_t prime_need_trace_id(
    PrimeNeedTraceId **items, size_t *len, uint64_t raw) {
    if (!items || !len || raw == 0u)
        return 0u;
    for (size_t i = 0u; i < *len; i++)
        if ((*items)[i].raw == raw)
            return (*items)[i].local;
    if (*len == SIZE_MAX / sizeof(**items))
        return 0u;
    PrimeNeedTraceId *next = realloc(
        *items, sizeof(**items) * (*len + 1u));
    if (!next)
        return 0u;
    *items = next;
    uint64_t local = (uint64_t)(*len + 1u);
    next[*len] = (PrimeNeedTraceId){.raw = raw, .local = local};
    (*len)++;
    return local;
}

static uint64_t prime_need_trace_cell(
    PrimeNeedTracePrinter *trace, uint64_t session, uint64_t thunk) {
    if (!trace || session == 0u || thunk == 0u)
        return 0u;
    for (size_t i = 0u; i < trace->cells_len; i++)
        if (trace->cells[i].session == session &&
            trace->cells[i].thunk == thunk)
            return trace->cells[i].local;
    if (trace->cells_len == SIZE_MAX / sizeof(*trace->cells))
        return 0u;
    PrimeNeedTraceCell *next = realloc(
        trace->cells,
        sizeof(*trace->cells) * (trace->cells_len + 1u));
    if (!next)
        return 0u;
    trace->cells = next;
    uint64_t local = (uint64_t)(trace->cells_len + 1u);
    next[trace->cells_len++] = (PrimeNeedTraceCell){
        .session = session, .thunk = thunk, .local = local,
    };
    return local;
}

static uint64_t prime_need_trace_state(
    PrimeNeedTracePrinter *trace, StateCell *state) {
    if (!trace || !state)
        return 0u;
    for (size_t i = 0u; i < trace->states_len; i++)
        if (trace->states[i].raw == state)
            return trace->states[i].local;
    if (trace->states_len == SIZE_MAX / sizeof(*trace->states))
        return 0u;
    PrimeNeedTraceState *next = realloc(
        trace->states,
        sizeof(*trace->states) * (trace->states_len + 1u));
    if (!next)
        return 0u;
    trace->states = next;
    uint64_t local = (uint64_t)(trace->states_len + 1u);
    next[trace->states_len++] = (PrimeNeedTraceState){
        .raw = state, .local = local,
    };
    return local;
}

static const char *prime_need_trace_event_name(
    PrimeNeedReceiptEventKind kind) {
    switch (kind) {
    case PRIME_NEED_RECEIPT_OBSERVE_CELL:
        return "observe-cell";
    case PRIME_NEED_RECEIPT_INSPECT_ORIGIN:
        return "inspect-origin";
    case PRIME_NEED_RECEIPT_READ_STATE:
        return "read-state";
    case PRIME_NEED_RECEIPT_WRITE_STATE:
        return "write-state";
    case PRIME_NEED_RECEIPT_USE_EQUATION:
        return "use-equation";
    case PRIME_NEED_RECEIPT_RESAMPLE:
        return "resample";
    }
    return "unknown";
}

static void prime_need_trace_answer(
    Atom *answer, const PrimeNeedReceipt *receipt, void *context) {
    PrimeNeedTracePrinter *trace = context;
    if (!trace || !trace->out || !answer)
        return;
    trace->answer++;
    uint64_t receipt_id = receipt
        ? prime_need_trace_id(
              &trace->receipts, &trace->receipts_len,
              receipt->session_id)
        : 0u;
    if (receipt && receipt->session_id != 0u && receipt_id == 0u)
        trace->allocation_failed = true;
    size_t event_count = receipt
        ? prime_need_receipt_event_count(receipt) : 0u;
    fprintf(trace->out,
            "(prime-need:answer %" PRIu64 " %" PRIu64 " (value ",
            trace->form, trace->answer);
    atom_print(answer, trace->out);
    fprintf(trace->out, ") (receipt %" PRIu64,
            receipt_id);
    for (size_t i = 0u; i < event_count; i++) {
        PrimeNeedReceiptEvent event;
        if (!prime_need_receipt_event_at(receipt, i, &event))
            continue;
        uint64_t event_id = prime_need_trace_id(
            &trace->events, &trace->events_len, event.event_id);
        if (event.event_id != 0u && event_id == 0u)
            trace->allocation_failed = true;
        fprintf(trace->out, " (event %" PRIu64 " %s",
                event_id, prime_need_trace_event_name(event.kind));
        if (event.need_session_id != 0u) {
            uint64_t session_id = prime_need_trace_id(
                &trace->sessions, &trace->sessions_len,
                event.need_session_id);
            if (session_id == 0u)
                trace->allocation_failed = true;
            fprintf(trace->out, " (need-session %" PRIu64 ")",
                    session_id);
        }
        if (event.source_occurrence_id != 0u) {
            uint64_t source_id = prime_need_trace_id(
                &trace->sources, &trace->sources_len,
                event.source_occurrence_id);
            if (source_id == 0u)
                trace->allocation_failed = true;
            fprintf(trace->out, " (application %" PRIu64 ")", source_id);
        }
        if (event.kind == PRIME_NEED_RECEIPT_OBSERVE_CELL &&
            event.source_occurrence_id != 0u)
            fprintf(trace->out, " (argument %" PRIu64 ")",
                    event.source_argument_index + 1u);
        if (event.need_session_id != 0u && event.thunk_id != 0u) {
            uint64_t cell_id = prime_need_trace_cell(
                trace, event.need_session_id, event.thunk_id);
            if (cell_id == 0u)
                trace->allocation_failed = true;
            fprintf(trace->out, " (cell %" PRIu64 ")", cell_id);
        }
        if (event.rule_occurrence_id != 0u) {
            uint64_t rule_id = prime_need_trace_id(
                &trace->rules, &trace->rules_len,
                event.rule_occurrence_id);
            if (rule_id == 0u)
                trace->allocation_failed = true;
            fprintf(trace->out, " (rule %" PRIu64 ")", rule_id);
        }
        if (event.state_cell) {
            uint64_t state_id = prime_need_trace_state(
                trace, event.state_cell);
            if (state_id == 0u)
                trace->allocation_failed = true;
            fprintf(trace->out, " (state %" PRIu64 ")", state_id);
        }
        if (event.before) {
            fputs(" (before ", trace->out);
            atom_print(event.before, trace->out);
            fputc(')', trace->out);
        }
        if (event.after) {
            fputs(" (after ", trace->out);
            atom_print(event.after, trace->out);
            fputc(')', trace->out);
        }
        fputc(')', trace->out);
    }
    fputs("))\n", trace->out);
}
#else
static void prime_need_trace_answer(
    Atom *answer, const PrimeNeedReceipt *receipt, void *context) {
    (void)answer;
    (void)receipt;
    (void)context;
}
#endif

static void prime_need_trace_printer_free(PrimeNeedTracePrinter *trace) {
    if (!trace)
        return;
    free(trace->events);
    free(trace->sessions);
    free(trace->sources);
    free(trace->rules);
    free(trace->receipts);
    free(trace->cells);
    free(trace->states);
    memset(trace, 0, sizeof(*trace));
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
    map->entries[map->len].preferred_name = NULL;
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
        if (atom->name_key) return true;
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
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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

static bool display_var_name_is_assigned(const CettaDisplayVarMap *map,
                                         const char *name) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->entries[i].display_name &&
            strcmp(map->entries[i].display_name, name) == 0) {
            return true;
        }
    }
    return false;
}

static bool display_var_name_is_preferred(const CettaDisplayVarMap *map,
                                          const char *name) {
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->entries[i].preferred_name &&
            strcmp(map->entries[i].preferred_name, name) == 0) {
            return true;
        }
    }
    return false;
}

static char *display_var_suffixed_name(Arena *arena, const char *base,
                                       uint32_t suffix) {
    char suffix_text[32];
    int suffix_len = snprintf(suffix_text, sizeof(suffix_text), "_%u", suffix);
    if (suffix_len < 0 || (size_t)suffix_len >= sizeof(suffix_text)) return NULL;
    size_t base_len = strlen(base);
    if (base_len > SIZE_MAX - (size_t)suffix_len - 1u) return NULL;
    char *name = arena_alloc(arena, base_len + (size_t)suffix_len + 1u);
    if (!name) return NULL;
    memcpy(name, base, base_len);
    memcpy(name + base_len, suffix_text, (size_t)suffix_len + 1u);
    return name;
}

static bool display_var_finalize(CettaDisplayVarMap *map, Arena *arena,
                                 bool pretty_namespaces) {
    for (uint32_t i = 0; i < map->len; i++) {
        map->entries[i].preferred_name = display_namespace_name(
            arena, map->entries[i].base_name, pretty_namespaces);
    }
    for (uint32_t i = 0; i < map->len; i++) {
        const char *base = map->entries[i].base_name;
        const char *display_base = map->entries[i].preferred_name;
        uint32_t group_size = 0;
        uint32_t rank = 0;
        for (uint32_t j = 0; j < map->len; j++) {
            if (strcmp(map->entries[j].base_name, base) != 0) continue;
            group_size++;
            if (map->entries[j].var_id < map->entries[i].var_id) {
                rank++;
            }
        }
        if ((group_size <= 1 || rank == 0) &&
            !display_var_name_is_assigned(map, display_base)) {
            map->entries[i].display_name = display_base;
            continue;
        }
        uint32_t suffix = rank ? rank : 1u;
        char *candidate = NULL;
        do {
            candidate = display_var_suffixed_name(arena, display_base, suffix);
            if (!candidate) return false;
            if (suffix == UINT32_MAX &&
                (display_var_name_is_preferred(map, candidate) ||
                 display_var_name_is_assigned(map, candidate))) {
                return false;
            }
            suffix++;
        } while (display_var_name_is_preferred(map, candidate) ||
                 display_var_name_is_assigned(map, candidate));
        map->entries[i].display_name = candidate;
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
        if (src->name_key)
            return atom_var_with_presentation(
                dst, SYMBOL_ID_NONE, src->name_key, src->var_id);
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
        case GV_BIGINT:
            return atom_bigint_copy(dst, src);
        case GV_RATIONAL:
            return atom_rational(dst, atom_rational_cstr(src));
        case GV_SPACE:
            return atom_space(dst, src->ground.ptr);
        case GV_CAPTURE:
            return atom_capture(dst, (CaptureClosure *)src->ground.ptr);
        case GV_FOREIGN:
            return atom_foreign(dst, (CettaForeignValue *)src->ground.ptr);
        case GV_PRIME_NEED_CAPABILITY: {
            const CettaPrimeNeedCapability *capability =
                atom_prime_need_capability_value(src);
            return capability
                       ? atom_prime_need_capability_with_rights(
                             dst, capability->session_id,
                             capability->thunk_id, capability->authority_id,
                             capability->rights)
                       : NULL;
        }
        case GV_PRIME_CONTEXT:
            return atom_deep_copy(dst, src);
        case GV_INTERNAL_TAG:
            return atom_internal_tag(
                dst, (CettaInternalTag)src->ground.ival);
        case GV_STATE: {
            StateCell *src_cell = (StateCell *)src->ground.ptr;
            StateCell *dst_cell = arena_alloc(dst, sizeof(StateCell));
            dst_cell->payload_owner_epoch = 0;
            dst_cell->payload_export_owner_epoch = 0;
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
        for (CettaExprIndex i = 0; i < src->expr.len; i++) {
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

static bool mm2_read_u32_be(const uint8_t *packet, size_t packet_len,
                            size_t *offset, uint32_t *out_value) {
    if (!packet || !offset || !out_value || *offset > packet_len ||
        packet_len - *offset < 4u) {
        return false;
    }
    *out_value = ((uint32_t)packet[*offset] << 24) |
                 ((uint32_t)packet[*offset + 1u] << 16) |
                 ((uint32_t)packet[*offset + 2u] << 8) |
                 (uint32_t)packet[*offset + 3u];
    *offset += 4u;
    return true;
}

static bool mm2_decode_expr_rows(Arena *arena,
                                 const uint8_t *packet,
                                 size_t packet_len,
                                 uint64_t row_count,
                                 Atom ***out_atoms,
                                 const char **out_error) {
    if (out_atoms)
        *out_atoms = NULL;
    if (out_error)
        *out_error = NULL;
    if (!arena || (!packet && packet_len != 0u) || !out_atoms ||
        row_count > SIZE_MAX / sizeof(Atom *)) {
        if (out_error)
            *out_error = "invalid MM2 bridge observation packet";
        return false;
    }

    Atom **atoms = row_count
        ? cetta_malloc(sizeof(Atom *) * (size_t)row_count)
        : NULL;
    size_t offset = 0u;
    for (uint64_t row = 0u; row < row_count; row++) {
        uint32_t expr_len = 0u;
        if (!mm2_read_u32_be(packet, packet_len, &offset, &expr_len) ||
            expr_len == 0u || offset > packet_len ||
            (size_t)expr_len > packet_len - offset ||
            !cetta_mm2_bridge_expr_packet_to_atom(
                arena, packet + offset, expr_len,
                &atoms[(size_t)row], out_error)) {
            free(atoms);
            if (out_error && !*out_error)
                *out_error = "malformed MM2 bridge observation row";
            return false;
        }
        offset += expr_len;
    }
    if (offset != packet_len) {
        free(atoms);
        if (out_error)
            *out_error = "MM2 bridge observation has trailing rows";
        return false;
    }
    *out_atoms = atoms;
    return true;
}

static bool mm2_print_alpha_canonical_atoms(Arena *arena,
                                            Atom *const *atoms,
                                            size_t atom_count,
                                            FILE *out,
                                            const char **out_error) {
    if (out_error)
        *out_error = NULL;
    if (!arena || (!atoms && atom_count != 0u) || !out) {
        if (out_error)
            *out_error = "invalid MM2 canonical observation arguments";
        return false;
    }
    Atom **canonical = atom_count
        ? cetta_malloc(sizeof(Atom *) * atom_count)
        : NULL;
    for (size_t index = 0u; index < atom_count; index++) {
        canonical[index] = cetta_mm2_canonical_surface_atom(
            arena, atoms[index], out_error);
        if (!canonical[index]) {
            free(canonical);
            return false;
        }
    }
    for (size_t index = 0u; index < atom_count; index++) {
        atom_print(canonical[index], out);
        fputc('\n', out);
    }
    free(canonical);
    return true;
}

static int run_mm2_program_via_mork(Arena *arena, Atom **atoms, int n,
                                    bool count_only, uint64_t step_limit) {
    CettaMorkSpaceHandle *space = NULL;
    uint64_t ignored = 0;
    uint64_t size = 0;
    uint8_t *dump = NULL;
    size_t dump_len = 0;
    uint64_t dump_rows = 0;
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

static int run_mm2_program_via_gslt(
    Arena *arena, Atom **atoms, int n, bool count_only, uint64_t step_limit,
    CettaMm2GsltRealization realization) {
    bool physical_supported = false;
    if (realization != CETTA_MM2_GSLT_REALIZATION_NATIVE_C &&
        cetta_mork_bridge_is_available() &&
        cetta_mork_bridge_support_transform_profile_supported_v1(
            cetta_mm2_gslt_profile_v1.physical_profile_packet,
            cetta_mm2_gslt_profile_v1.physical_profile_packet_size,
            &physical_supported) &&
        physical_supported) {
        CettaMorkSpaceHandle *space = cetta_mork_bridge_space_new();
        if (!space) {
            fprintf(stderr,
                    "error: MM2 GSLT physical space allocation failed: %s\n",
                    cetta_mork_bridge_last_error());
            return 1;
        }
        uint64_t ignored = 0u;
        uint64_t performed = 0u;
        uint64_t final_size = 0u;
        uint8_t *rows_packet = NULL;
        size_t rows_packet_len = 0u;
        uint64_t dump_rows = 0u;
        bool has_work = false;
        int physical_rc = 0;
        for (int index = 0; index < n; index++) {
            char *surface = cetta_mm2_atom_to_surface_string(
                arena, atoms[index]);
            if (!cetta_mork_bridge_space_add_text(
                    space, surface, &ignored)) {
                fprintf(stderr,
                        "error: MM2 GSLT physical load failed: %s\n",
                        cetta_mork_bridge_last_error());
                physical_rc = 1;
                goto physical_done;
            }
        }
        if (!cetta_mork_bridge_space_step_support_transform_v1(
                space,
                cetta_mm2_gslt_profile_v1.physical_profile_packet,
                cetta_mm2_gslt_profile_v1.physical_profile_packet_size,
                step_limit, &performed) ||
            !cetta_mork_bridge_space_has_support_transform_work_v1(
                space,
                cetta_mm2_gslt_profile_v1.physical_profile_packet,
                cetta_mm2_gslt_profile_v1.physical_profile_packet_size,
                &has_work) ||
            !cetta_mork_bridge_space_size(space, &final_size)) {
            fprintf(stderr,
                    "error: MM2 GSLT physical execution failed: %s\n",
                    cetta_mork_bridge_last_error());
            physical_rc = 1;
            goto physical_done;
        }
        if (count_only) {
            fprintf(stdout, "%" PRIu64 "\n", final_size);
        } else {
            Atom **observed = NULL;
            const char *decode_error = NULL;
            if (!cetta_mork_bridge_space_dump_expr_rows(
                    space, &rows_packet, &rows_packet_len, &dump_rows)) {
                fprintf(stderr,
                        "error: MM2 GSLT physical observation failed: %s\n",
                        cetta_mork_bridge_last_error());
                physical_rc = 1;
                goto physical_done;
            }
            if (dump_rows != final_size ||
                !mm2_decode_expr_rows(
                    arena, rows_packet, rows_packet_len, dump_rows,
                    &observed, &decode_error)) {
                fprintf(stderr,
                        "error: MM2 GSLT physical observation decode failed: %s\n",
                        decode_error ? decode_error : "row count mismatch");
                physical_rc = 1;
                goto physical_done;
            }
            for (uint64_t row = 0u; row < dump_rows; row++) {
                Atom *surface = cetta_mm2_canonical_surface_atom(
                    arena, observed[(size_t)row], &decode_error);
                if (!surface) {
                    fprintf(stderr,
                            "error: MM2 GSLT physical observation could not "
                            "be projected: %s\n",
                            decode_error ? decode_error : "unknown bridge error");
                    free(observed);
                    physical_rc = 1;
                    goto physical_done;
                }
                atom_print(surface, stdout);
                fputc('\n', stdout);
            }
            free(observed);
        }
        if (has_work) {
            fprintf(stderr,
                    "(Mm2GsltStatus expired steps=%" PRIu64
                    " residual-atoms=%" PRIu64 ")\n",
                    performed, final_size);
            physical_rc = CETTA_MM2_GSLT_EXIT_EXPIRED;
        }

physical_done:
        cetta_mork_bridge_bytes_free(rows_packet, rows_packet_len);
        cetta_mork_bridge_space_free(space);
        return physical_rc;
    }
    if (realization == CETTA_MM2_GSLT_REALIZATION_RUST_PATHMAP_ABI) {
        fprintf(stderr,
                "error: requested MM2 GSLT Rust/PathMap via C ABI "
                "realization is unavailable: %s\n",
                cetta_mork_bridge_last_error());
        return 2;
    }

    CettaGsltSupportTransformResultV1 result = {0};
    char error[512] = {0};
    if (!cetta_gslt_support_transform_run_v1(
            &cetta_mm2_gslt_profile_v1, arena, atoms, (size_t)n,
            step_limit, &result, error, sizeof(error))) {
        fprintf(stderr, "error: MM2 GSLT execution failed: %s\n",
                error[0] ? error : "unknown support-transform fault");
        cetta_gslt_support_transform_result_free_v1(&result);
        return 1;
    }

    int rc = 0;
    const char *canonical_error = NULL;
    if (count_only) {
        fprintf(stdout, "%zu\n", result.atom_count);
    } else if (!mm2_print_alpha_canonical_atoms(
                   arena, result.atoms, result.atom_count,
                   stdout, &canonical_error)) {
        fprintf(stderr,
                "error: MM2 GSLT reference observation could not be "
                "canonicalized: %s\n",
                canonical_error ? canonical_error : "unknown bridge error");
        rc = 1;
    }
    if (rc == 0 && result.outcome == CETTA_GSLT_SUPPORT_EXPIRED) {
        fprintf(stderr,
                "(Mm2GsltStatus expired steps=%" PRIu64
                " residual-atoms=%zu)\n",
                result.steps, result.atom_count);
        rc = CETTA_MM2_GSLT_EXIT_EXPIRED;
    }
    cetta_gslt_support_transform_result_free_v1(&result);
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
    uint64_t dump_rows = 0;
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

static void write_results(FILE *out, ResultSet *rs,
                          CettaLanguageId language_id,
                          const CettaProfile *profile) {
    FILE *logical_dest = stdout;
    ResultSet visible = {0};
    Atom **visible_items = NULL;
    uint32_t visible_len = 0;
    bool rust_compat =
        cetta_language_uses_rust_he_compat_semantics(language_id, profile);
    if (rs->len == 0) {
        if (rust_compat) fprintf(out, "[]\n");
        return;
    }

    bool hide_legacy_empty = language_id != CETTA_LANGUAGE_PRIME &&
        !cetta_language_uses_embedded_gslt(language_id);
    for (uint32_t i = 0; i < rs->len; i++) {
        if (!hide_legacy_empty || !atom_is_empty(rs->items[i]))
            visible_len++;
    }
    if (visible_len == 0) {
        if (rust_compat) fprintf(out, "[]\n");
        return;
    }
    if (visible_len != rs->len) {
        visible_items = malloc(sizeof(Atom *) * visible_len);
        if (!visible_items) return;
        uint32_t out_i = 0;
        for (uint32_t i = 0; i < rs->len; i++) {
            if (!hide_legacy_empty || !atom_is_empty(rs->items[i]))
                visible_items[out_i++] = rs->items[i];
        }
        visible.items = visible_items;
        visible.len = visible_len;
        visible.cap = visible_len;
        rs = &visible;
    }

    /*
     * PeTTa's runnable driver exposes the answer stream one result per line.
     * The surrounding result vector is CeTTa's host API container, not part
     * of PeTTa syntax, so do not print the HE-style square-bracket envelope.
     */
    if (language_id == CETTA_LANGUAGE_PETTA) {
        for (uint32_t index = 0u; index < rs->len; index++) {
            atom_print_petta(rs->items[index], out);
            fputc('\n', out);
        }
        goto done;
    }

    if (g_count_only) {
        if (rs->len == 1 &&
            rs->items[0]->kind == ATOM_GROUNDED &&
            rs->items[0]->ground.gkind == GV_INT) {
            fprintf(out, "%lld\n", (long long)rs->items[0]->ground.ival);
            goto done;
        }
        fprintf(out, "%" PRIu64 "\n", rs->len);
        goto done;
    }
    if (g_quiet_results && !result_set_has_error(rs) &&
        result_set_all_empty(rs, language_id)) {
        goto done;
    }
    if (result_set_all_rhocalc_domain(rs)) {
        fprintf(out, "[");
        for (uint32_t i = 0; i < rs->len; i++) {
            if (i > 0) fprintf(out, ", ");
            rhocalc_print_atom_syntax(rs->items[i], CETTA_SYNTAX_MRHO, out);
        }
        fprintf(out, "]\n");
        goto done;
    }
    bool pretty_vars = display_vars_pretty_enabled_for(logical_dest);
    bool pretty_namespaces = display_namespaces_pretty_enabled_for(logical_dest);
    if (pretty_vars || pretty_namespaces) {
        if (write_pretty_results(out, rs, pretty_vars, pretty_namespaces)) {
            goto done;
        }
    }
    fprintf(out, "[");
    for (uint32_t i = 0; i < rs->len; i++) {
        if (i > 0) fprintf(out, ", ");
        if (rhocalc_is_domain_atom(rs->items[i])) {
            rhocalc_print_atom_syntax(rs->items[i], CETTA_SYNTAX_MRHO, out);
        } else {
            atom_print(rs->items[i], out);
        }
    }
    fprintf(out, "]\n");

done:
    free(visible_items);
}

typedef struct {
    const char *lang_name;
    const char *profile_name;
    CettaSyntaxId syntax;
    const CettaLanguageSpec *lang;
    const CettaProfile *profile;
    char langdef_manifest_path[PATH_MAX];
} CettaCliEndpoint;

static void endpoint_init(CettaCliEndpoint *endpoint, const char *lang_name) {
    endpoint->lang_name = lang_name;
    endpoint->profile_name = NULL;
    endpoint->syntax = CETTA_SYNTAX_AUTO;
    endpoint->lang = NULL;
    endpoint->profile = NULL;
    endpoint->langdef_manifest_path[0] = '\0';
}

static bool endpoint_resolve(CettaCliEndpoint *endpoint, const char *role,
                             const char *exec_path) {
    char langdef_error[256] = {0};

    endpoint->lang = cetta_language_lookup(endpoint->lang_name);
    if (!endpoint->lang) {
        if (cetta_langdef_resolve_named_manifest_v1(
                exec_path, endpoint->lang_name,
                endpoint->langdef_manifest_path,
                sizeof(endpoint->langdef_manifest_path),
                langdef_error, sizeof(langdef_error))) {
            if (endpoint->profile_name) {
                fprintf(stderr,
                        "error: %s language definition '%s' has no named profiles\n",
                        role, endpoint->lang_name);
                return false;
            }
            return true;
        }
        fprintf(stderr, "error: unknown %s language '%s'\n",
                role, endpoint->lang_name);
        cetta_language_print_inventory(stderr);
        return false;
    }
    if (!endpoint->lang->implemented) {
        fprintf(stderr,
                "error: %s language '%s' is recognized but not implemented in cetta yet\n",
                role, endpoint->lang_name);
        fprintf(stderr, "note: %s\n", endpoint->lang->note);
        return false;
    }
    if (endpoint->profile_name) {
        if (!cetta_language_has_named_profiles(endpoint->lang->id)) {
            fprintf(stderr, "error: %s language '%s' has no named profiles\n",
                    role, endpoint->lang->canonical);
            return false;
        }
        endpoint->profile =
            cetta_profile_from_name_for_language(endpoint->lang->id,
                                                 endpoint->profile_name);
        if (!endpoint->profile) {
            fprintf(stderr, "error: unknown %s profile '%s' for language '%s'\n",
                    role, endpoint->profile_name, endpoint->lang->canonical);
            cetta_profile_print_inventory_for_language(stderr, endpoint->lang->id);
            return false;
        }
    }
    if (!endpoint->profile && endpoint->lang->id == CETTA_LANGUAGE_PRIME)
        endpoint->profile = cetta_profile_prime_default();
    return true;
}

static CettaSyntaxId endpoint_effective_syntax(const CettaCliEndpoint *endpoint,
                                               const char *filename) {
    if (endpoint->syntax != CETTA_SYNTAX_AUTO) {
        return endpoint->syntax;
    }
    return cetta_syntax_infer_for_path(endpoint->lang->id, filename);
}

static bool endpoint_supports_syntax(const CettaCliEndpoint *endpoint,
                                     CettaSyntaxId syntax,
                                     const char *role) {
    if (cetta_language_supports_syntax(endpoint->lang->id, syntax)) {
        return true;
    }
    fprintf(stderr, "error: %s syntax '%s' is not supported for --lang %s\n",
            role, cetta_syntax_name(syntax), endpoint->lang->canonical);
    return false;
}

static RhocalcSemanticProfileId
rhocalc_semantic_profile_for_endpoint(const CettaCliEndpoint *endpoint) {
    if (!endpoint || !endpoint->profile) {
        return RHOCALC_SEMANTIC_PROFILE_STRICT_CORE;
    }
    switch (endpoint->profile->id) {
    case CETTA_PROFILE_RHOCALC_COST:
        return RHOCALC_SEMANTIC_PROFILE_COST;
    case CETTA_PROFILE_RHOCALC_STRICT_CORE:
    default:
        return RHOCALC_SEMANTIC_PROFILE_STRICT_CORE;
    }
}

static bool rho_scheduler_policy_from_name(const char *name,
                                           RhoSchedulerPolicy *out) {
    if (!name || !out) return false;
    if (strcmp(name, "canonical") == 0) {
        *out = RHO_SCHEDULER_CANONICAL;
        return true;
    }
    if (strcmp(name, "rotating") == 0) {
        *out = RHO_SCHEDULER_ROTATING;
        return true;
    }
    return false;
}

static int run_rhocalc_cli(const char *filename,
                           const char *inline_text,
                           RhocalcSemanticProfileId semantic_profile,
                           CettaSyntaxId syntax,
                           const RhoRuntimeProfile *profile) {
    int rc = 0;
    int n = 0;
    Atom **atoms = NULL;
    Arena arena;
    SymbolTable symbol_table;
    VarInternTable var_intern_table;
    HashConsTable hashcons_table;

    arena_init(&arena);
    symbol_table_init(&symbol_table);
    symbol_table_init_builtins(&symbol_table, &g_builtin_syms);
    var_intern_init(&var_intern_table);
    hashcons_init(&hashcons_table);
    g_symbols = &symbol_table;
    g_var_intern = &var_intern_table;
    g_hashcons = &hashcons_table;
    arena_set_hashcons(&arena, &hashcons_table);

    n = inline_text
        ? rhocalc_parse_text(inline_text, semantic_profile, syntax, &arena, &atoms)
        : rhocalc_parse_file(filename, semantic_profile, syntax, &arena, &atoms);
    if (n < 0) {
        const char *detail = rhocalc_last_parse_error();
        fprintf(stderr, "error: could not parse %s as rhocalc/%s\n",
                inline_text ? "inline input" : filename,
                cetta_syntax_name(syntax));
        if (detail) fprintf(stderr, "note: %s\n", detail);
        rc = 1;
        goto done;
    }

    for (int i = 0; i < n; i++) {
        RhoReductionResult reduction = {0};
        if (!rhocalc_reduce_to_quiescence_with_semantic_profile(&arena, atoms[i],
                                                                semantic_profile,
                                                                profile, &reduction)) {
            const char *detail = rhocalc_last_validation_error();
            if (semantic_profile == RHOCALC_SEMANTIC_PROFILE_STRICT_CORE) {
                fprintf(stderr, "error: invalid rhocalc core process");
            } else {
                fprintf(stderr, "error: invalid rhocalc %s term",
                        rhocalc_semantic_profile_name(semantic_profile));
            }
            if (detail) fprintf(stderr, ": %s", detail);
            fputc('\n', stderr);
            rc = 1;
            goto done;
        }
        rhocalc_print_atom_syntax(reduction.residual, syntax, stdout);
        fputc('\n', stdout);
        if (reduction.status == RHOCALC_REDUCTION_LIMIT_EXHAUSTED) {
            fflush(stdout);
            fprintf(stderr,
                    "warning: rhocalc reduction limit exhausted after %u COMM %s\n",
                    reduction.reductions_taken,
                    reduction.reductions_taken == 1 ? "reduction" : "reductions");
            rc = CETTA_RHOCALC_EXIT_REDUCTION_LIMIT_EXHAUSTED;
            goto done;
        }
    }

done:
    free(atoms);
    g_hashcons = NULL;
    g_var_intern = NULL;
    g_symbols = NULL;
    hashcons_free(&hashcons_table);
    var_intern_free(&var_intern_table);
    symbol_table_free(&symbol_table);
    arena_free(&arena);
    return rc;
}

static int run_rhocalc_translation(const char *filename,
                                   const char *inline_text,
                                   RhocalcSemanticProfileId input_profile,
                                   RhocalcSemanticProfileId output_profile,
                                   CettaSyntaxId input_syntax,
                                   CettaSyntaxId output_syntax) {
    int rc = 0;
    int n = 0;
    Atom **atoms = NULL;
    Arena arena;
    SymbolTable symbol_table;
    VarInternTable var_intern_table;
    HashConsTable hashcons_table;

    arena_init(&arena);
    symbol_table_init(&symbol_table);
    symbol_table_init_builtins(&symbol_table, &g_builtin_syms);
    var_intern_init(&var_intern_table);
    hashcons_init(&hashcons_table);
    g_symbols = &symbol_table;
    g_var_intern = &var_intern_table;
    g_hashcons = &hashcons_table;
    arena_set_hashcons(&arena, &hashcons_table);

    n = inline_text
        ? rhocalc_parse_text(inline_text, input_profile, input_syntax, &arena, &atoms)
        : rhocalc_parse_file(filename, input_profile, input_syntax, &arena, &atoms);
    if (n < 0) {
        const char *detail = rhocalc_last_parse_error();
        fprintf(stderr, "error: could not parse %s as rhocalc/%s\n",
                inline_text ? "inline input" : filename,
                cetta_syntax_name(input_syntax));
        if (detail) fprintf(stderr, "note: %s\n", detail);
        rc = 1;
        goto done;
    }
    if (output_profile != RHOCALC_SEMANTIC_PROFILE_STRICT_CORE) {
        fprintf(stderr,
                "error: rhocalc profile '%s' translation is not implemented yet\n",
                rhocalc_semantic_profile_name(output_profile));
        rc = 1;
        goto done;
    }
    for (int i = 0; i < n; i++) {
        if (!rhocalc_process_well_formed_with_semantic_profile(atoms[i],
                                                               input_profile)) {
            const char *detail = rhocalc_last_validation_error();
            if (input_profile == RHOCALC_SEMANTIC_PROFILE_STRICT_CORE) {
                fprintf(stderr, "error: invalid rhocalc core process");
            } else {
                fprintf(stderr, "error: invalid rhocalc %s term",
                        rhocalc_semantic_profile_name(input_profile));
            }
            if (detail) fprintf(stderr, ": %s", detail);
            fputc('\n', stderr);
            rc = 1;
            goto done;
        }
        rhocalc_print_atom_syntax(atoms[i], output_syntax, stdout);
        fputc('\n', stdout);
    }

done:
    free(atoms);
    g_hashcons = NULL;
    g_var_intern = NULL;
    g_symbols = NULL;
    hashcons_free(&hashcons_table);
    var_intern_free(&var_intern_table);
    symbol_table_free(&symbol_table);
    arena_free(&arena);
    return rc;
}

static void print_usage(FILE *out) {
    fputs("usage: cetta [--lang <name>] [--syntax <metta|mrho|rho>] <file>\n", out);
    fputs("       cetta -e '<expr>' [-e '<expr>' ...]  # inline expressions (multiple -e concatenate)\n", out);
    fputs("       cetta --translate --lang A [--syntax S] --lang B [--syntax T] <file>\n", out);
    fputs("       cetta [--lang he --profile <he|he-compat|he-extended|he-prime>] <file.metta>\n", out);
    fputs("       cetta --lang prime <file.metta>\n", out);
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
    fputs("       cetta --lang petta --profile typecheck-v2 [--strict|--strict-det] <file.metta>\n", out);
#endif
    fputs("       cetta [--lang rhocalc --profile <strict-core|cost>] [--syntax <mrho|rho>] <file>\n", out);
    fputs("       cetta [--lang <name>] [--import-mode <upstream|relative|ancestor-walk>] <file.metta>\n", out);
#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
    fputs("       cetta --lang <langdef> --langdef-proof-backend <authority|generated-relational-audit-v1|frame-cache-diagnostic-v1> <file>\n", out);
#else
    fputs("       cetta --lang <langdef> --langdef-proof-backend <authority|generated-relational-audit-v1> <file>\n", out);
#endif
    fputs("       note: repeated --lang under --translate means source then target endpoint\n", out);
    fputs("       note: --lang petta consumes consecutive .metta files in order\n", out);
    fputs("       note: other languages run one file; remaining arguments are available through system:args\n", out);
    fputs("       cetta --help | -h                    # print this usage summary\n", out);
    fputs("       cetta --version | -v                 # print binary version and build mode\n", out);
    fputs("       cetta --compile <file.metta>           # emit LLVM IR to stdout\n", out);
    fputs("       cetta --compile-stdlib <file.metta>     # emit precompiled stdlib blob to stdout\n", out);
    fputs("       cetta --count-only <file.metta>        # print result counts only\n", out);
    fputs("       cetta --quiet <file.metta>              # hide pure [()] success clutter\n", out);
    fputs("       cetta --emit-runtime-stats <file.metta> # dump runtime counters to stderr after execution\n", out);
    fputs("       cetta --emit-prime-need-trace <file.metta> # emit exact Prime occurrences, receipts, and completion to stderr\n", out);
    fputs("       cetta --lang prime --prime-rewrite-frontier <monolithic|candidate-local|demand-cohort> <file.metta>\n", out);
    fputs("       cetta --eval-hashcons <file.metta>      # experimental: hash-cons eval-arena atoms\n", out);
    fputs("       cetta --pretty-vars <file.metta>       # pretty-print result vars for humans\n", out);
    fputs("       cetta --raw-vars <file.metta>          # print raw internal var epochs\n", out);
    fputs("       cetta --pretty-namespaces <file.metta> # pretty-print mork./runtime. namespace sugar\n", out);
    fputs("       cetta --raw-namespaces <file.metta>    # print canonical mork:/runtime: names\n", out);
    fputs("       cetta --prefer-rationals <file.metta>  # exact rational division for exact numbers\n", out);
    fputs("       cetta --fuel <n> <file.metta>          # override evaluator fuel budget\n", out);
    fputs("       cetta --num-threads <n> <file>             # set OS-thread budget for parallel-capable execution\n", out);
    fputs("       cetta --rho-reduction-limit <n> <file>            # run at most n strict-core rho COMM reductions (default 100000)\n", out);
    fputs("       cetta --rho-scheduler <canonical|rotating> <file> # select strict-core rho reduction policy\n", out);
    fputs("       cetta --lang mm2 [--steps <n>] <file.mm2> # upstream MORK ABI\n", out);
    fputs("       cetta --lang mm2 --profile gslt [--space-engine <native|pathmap>] [--steps <n>] <file.mm2>\n", out);
    fputs("       cetta --space-engine <name> <file.metta>\n", out);
    fputs("       cetta --space-match-backend <name> <file.metta>   # alias for --space-engine\n", out);
    fputs("       cetta --lang <zero|subzero|gslt-il|zerouv|metta-interact> --gslt-realization <horn-reference|compiled-worklist> <file.metta>\n", out);
    fputs("       cetta --lang zero <file.metta> # direct (eval subject) and (zero-query pattern template) requests; ! is not Zero syntax\n", out);
    fputs("       cetta --lang zero --profile emit <file.metta> # revision-threaded match/let/eval/emit; add-atom aliases persistent emit\n", out);
    fputs("       cetta --lang zero --profile interact [--space-engine <native|pathmap>] <file.metta> # authenticated immutable revisions; native C or Rust/PathMap via C ABI\n", out);
    fputs("       cetta --lang gslt-il <file.metta> # spaces, directed equations, routes, and one-step ! requests\n", out);
    fputs("       cetta --lang zerouv <file.metta> # productive step, finite paths, authored control, and recurrence articles\n", out);
    fputs("       cetta --lang metta-interact <file.metta> # revisioned events, continuations, and inspectable cost\n", out);
    fputs("       cetta [--lang <name>] --list-profiles\n", out);
    fputs("       cetta --list-space-engines\n", out);
    fputs("       cetta --list-space-match-backends                 # alias for --list-space-engines\n", out);
    fputs("       cetta --list-languages\n", out);
    fputs("\nMM2 execution contracts:\n", out);
    fputs("  default      upstream support-valued MORK ABI; least serialized exec atom first\n", out);
    fputs("  gslt         authored support-transform profile; native C or Rust/PathMap via C ABI\n", out);
    fputs("  gslt input   comma patterns, or I factors BTM / == / !=\n", out);
    fputs("  gslt output  comma additions, or O sinks + / - / head / tail / count / pure-f64\n", out);
    fputs("  gslt unknown unsupported exec directives remain inert in the final support\n", out);
    fputs("  gslt order   least full directive by MORK compact-expression byte order\n", out);
    fputs("  gslt plans   generated exec is decoded at runtime and cached by alpha-normalized structure\n", out);
    fputs("  gslt engine  native selects native C; pathmap selects Rust/PathMap via the C ABI\n", out);
    fputs("  gslt auto    without --space-engine, prefer Rust/PathMap when present, otherwise native C\n", out);
    fprintf(out,
            "  --steps n    exact external upper bound (default %" PRIu64 "); zero executes nothing\n",
            CETTA_MM2_DEFAULT_RUN_STEPS);
    fputs("  observation  final support dump; GSLT expiration is explicit and exits with status 3\n", out);
    fputs("  profiles     plain mm2 retains upstream MORK through its C ABI; gslt selects authored semantics\n", out);
    fputs("\nGSLT-IL authoring:\n", out);
    fputs("  (in &space (= left right))       directed rule in a named semantic space\n", out);
    fputs("  (route name &source &target)     forward operational route declaration\n", out);
    fputs("  (= (name left) right)            finite route action\n", out);
    fputs("  (! (in &space state)) | (! (name state))  one generating step; chaining is a runner layer\n", out);
    fputs("  route laws   identity/composition belong to the admitted diagram; exactness requires evidence\n", out);
}

static void print_version(FILE *out) {
    fprintf(out, "cetta %s (%s)\n", CETTA_VERSION_STRING, CETTA_BUILD_MODE_STRING);
}

static __attribute__((unused)) bool
compile_profile_guard_ok(CettaLanguageId language_id,
                         const CettaProfile *profile,
                         Atom **atoms,
                         int n) {
    (void)language_id;
    (void)profile;
    (void)atoms;
    (void)n;
    return true;
}

static __attribute__((unused)) bool
compile_profile_guard_ok_ids(CettaLanguageId language_id,
                             const CettaProfile *profile,
                             TermUniverse *universe,
                             const AtomId *atom_ids,
                             int n) {
    (void)language_id;
    (void)profile;
    (void)universe;
    (void)atom_ids;
    (void)n;
    return true;
}

static bool atom_id_is_symbol_id(const TermUniverse *universe, AtomId atom_id,
                                 SymbolId sym_id) {
    return universe && atom_id != CETTA_ATOM_ID_NONE &&
           tu_kind(universe, atom_id) == ATOM_SYMBOL &&
           tu_sym(universe, atom_id) == sym_id;
}

static bool main_try_add_builtin_type_decls_direct(Space *space,
                                                   CettaLanguageId language_id,
                                                   const CettaProfile *profile) {
    if (!space || !space->native.universe)
        return false;

    static const char *arith_ops_base[] = {"+", "-", "*", "/", "%", NULL};
    static const char *arith_ops_formal[] = {"+", "-", "*", "/", "//", "%", NULL};
    static const char *cmp_ops[] = {"<", ">", "<=", ">=", NULL};
    const char **arith_ops =
        cetta_language_uses_rust_he_compat_semantics(language_id, profile)
        ? arith_ops_base
        : arith_ops_formal;

    TermUniverse *universe = space->native.universe;
    AtomId decl_ids[12];
    uint32_t decl_count = 0;

    AtomId colon_id = tu_intern_symbol(universe, g_builtin_syms.colon);
    AtomId arrow_id = tu_intern_symbol(universe, g_builtin_syms.arrow);
    AtomId equals_id = tu_intern_symbol(universe, g_builtin_syms.equals);
    AtomId eqeq_id = tu_intern_symbol(universe, symbol_intern_cstr(g_symbols, "=="));
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

    for (uint32_t i = 0; i < decl_count; i++)
        space_add_atom_id(space, decl_ids[i]);
    return true;
}

/* he-prime typing ops: the declared types are what STAGE the term/type
 * arguments (Atom = arrives unreduced), so argument evaluation cannot run a
 * computation before the checker has ruled on its admissibility.  Space and
 * fuel arguments evaluate normally.  The rows double as the ops' visible
 * self-description; the chainer ignores them (arrow-typed declarations
 * without a chaining-rule marker are excluded from its index). */
static void main_add_he_prime_typing_op_decls(Space *space, Arena *arena) {
    /* u = %Undefined% (evaluated), A = Atom (staged), N = Number */
    static const struct { const char *name; const char *sig; } ops[] = {
        {"normalize-type", "uAN"},
        {"check-type-refinements", "uAN"},
        {"is-consistent", "AA"},
        {"check-type", "uAAN"},
        {"search-inhabitants", "uANNN"},
        {"search-inhabitants", "uANNNA"},
        {"search-first-inhabitant", "uAN"},
        {"search-first-inhabitant", "uANN"},
        {"search-first-inhabitant", "uANNA"},
        {"type-forward-step", "uNN"},
        {"type-forward-closure", "uNNN"},
    };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        const char *sig = ops[i].sig;
        size_t n = strlen(sig);
        Atom **elems = arena_alloc(arena, sizeof(Atom *) * (n + 2));
        elems[0] = atom_symbol_id(arena, g_builtin_syms.arrow);
        for (size_t k = 0; k < n; k++) {
            elems[k + 1] = sig[k] == 'A'
                ? atom_symbol_id(arena, g_builtin_syms.atom)
                : sig[k] == 'N' ? atom_symbol(arena, "Number")
                                : atom_undefined_type(arena);
        }
        elems[n + 1] = atom_undefined_type(arena);
        Atom *decl = atom_expr3(arena,
            atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, ops[i].name),
            atom_expr(arena, elems, (CettaExprLen)(n + 2)));
        if (!space_admit_atom(space, arena, decl))
            space_add(space, decl);
    }
}

static void main_add_prime_semantic_op_decls(Space *space, Arena *arena) {
    static const struct { const char *name; const char *sig; } ops[] = {
        {"prime-package", "u"},
        {"prime-judge", "uA"},
        {"prime-judge", "uAN"},
    };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        const char *sig = ops[i].sig;
        size_t n = strlen(sig);
        Atom **elems = arena_alloc(arena, sizeof(Atom *) * (n + 2));
        elems[0] = atom_symbol_id(arena, g_builtin_syms.arrow);
        for (size_t k = 0; k < n; k++) {
            elems[k + 1] = sig[k] == 'A'
                ? atom_symbol_id(arena, g_builtin_syms.atom)
                : sig[k] == 'N' ? atom_symbol(arena, "Number")
                                : atom_undefined_type(arena);
        }
        elems[n + 1] = atom_undefined_type(arena);
        Atom *decl = atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, ops[i].name),
            atom_expr(arena, elems, (CettaExprLen)(n + 2)));
        if (!space_admit_atom(space, arena, decl))
            space_add(space, decl);
    }

    Atom *a_var = atom_var(arena, "a");
    Atom *suspension_a = atom_expr2(
        arena, atom_symbol(arena, "suspension"), a_var);
    Atom *delay_type = atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.arrow),
        a_var, suspension_a);
    Atom *force_type = atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.arrow),
        suspension_a, a_var);
    Atom *typed_ops[2] = {
        atom_expr3(arena,
                   atom_symbol_id(arena, g_builtin_syms.colon),
                   atom_symbol(arena, "delay"), delay_type),
        atom_expr3(arena,
                   atom_symbol_id(arena, g_builtin_syms.colon),
                   atom_symbol(arena, "force"), force_type),
    };
    const char *scheme_names[2] = {"delay", "force"};
    for (size_t i = 0u; i < 2u; i++) {
        if (!space_admit_atom(space, arena, typed_ops[i]))
            space_add(space, typed_ops[i]);
        Atom *scheme = atom_expr2(
            arena, atom_symbol(arena, "type-scheme"),
            atom_symbol(arena, scheme_names[i]));
        if (!space_admit_atom(space, arena, scheme))
            space_add(space, scheme);
    }

    Atom *context_type = atom_symbol(arena, "context");
    Atom *name_type = atom_symbol(arena, "Name");
    Atom *atom_type = atom_symbol_id(arena, g_builtin_syms.atom);
    Atom *number_type = atom_symbol(arena, "Number");
    Atom *ctx_bind_type = atom_expr(
        arena,
        (Atom *[]){
            atom_symbol_id(arena, g_builtin_syms.arrow),
            context_type, name_type, atom_type, context_type,
        },
        5u);
    Atom *ctx_get_type = atom_expr(
        arena,
        (Atom *[]){
            atom_symbol_id(arena, g_builtin_syms.arrow),
            context_type, name_type, atom_type,
        },
        4u);
    Atom *ctx_view_type = atom_expr(
        arena,
        (Atom *[]){
            atom_symbol_id(arena, g_builtin_syms.arrow),
            context_type, number_type, atom_type,
        },
        4u);
    Atom *context_decls[] = {
        atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, "ctx:empty"), context_type),
        atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, "ctx:bind"), ctx_bind_type),
        atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, "ctx:get"), ctx_get_type),
        atom_expr3(
            arena, atom_symbol_id(arena, g_builtin_syms.colon),
            atom_symbol(arena, "ctx:view"), ctx_view_type),
    };
    for (size_t i = 0u;
         i < sizeof context_decls / sizeof context_decls[0]; i++) {
        if (!space_admit_atom(space, arena, context_decls[i]))
            space_add(space, context_decls[i]);
    }
}

static void main_add_builtin_type_decls(Space *space, Arena *arena,
                                        CettaLanguageId language_id,
                                        const CettaProfile *profile) {
    if ((language_id == CETTA_LANGUAGE_HE ||
         language_id == CETTA_LANGUAGE_PRIME) &&
        profile && profile->enable_dependent_telescope)
        main_add_he_prime_typing_op_decls(space, arena);
    if (language_id == CETTA_LANGUAGE_PRIME)
        main_add_prime_semantic_op_decls(space, arena);
    if (main_try_add_builtin_type_decls_direct(space, language_id, profile))
        return;

    Atom *num = atom_symbol(arena, "Number");
    Atom *arrow_nnn = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), num, num, num}, 4);
    Atom *bool_t = atom_symbol(arena, "Bool");
    Atom *arrow_nnb = atom_expr(arena, (Atom*[]){
        atom_symbol_id(arena, g_builtin_syms.arrow), num, num, bool_t}, 4);
    static const char *arith_ops_base[] = {"+", "-", "*", "/", "%", NULL};
    static const char *arith_ops_formal[] = {"+", "-", "*", "/", "//", "%", NULL};
    const char **arith_ops =
        cetta_language_uses_rust_he_compat_semantics(language_id, profile)
        ? arith_ops_base
        : arith_ops_formal;
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
}

static int main_he_compiled_text_backend(
    void *context, const char *text, TermUniverse *universe,
    AtomId **out_ids, char *error_buf, size_t error_buf_size) {
    HECompiledReaderV1Receipt receipt;
    return he_compiled_reader_v1_parse_text_ids(
        context, text, universe, out_ids, &receipt,
        error_buf, error_buf_size);
}

static int main_he_compiled_file_backend(
    void *context, const char *filename, TermUniverse *universe,
    AtomId **out_ids, char *error_buf, size_t error_buf_size) {
    HECompiledReaderV1Receipt receipt;
    return he_compiled_reader_v1_parse_file_ids(
        context, filename, universe, out_ids, &receipt,
        error_buf, error_buf_size);
}

static int main_petta_compiled_text_backend(
    void *context, const char *text, TermUniverse *universe,
    AtomId **out_ids, char *error_buf, size_t error_buf_size) {
    PeTTaCompiledReaderV1Receipt receipt;
    return petta_compiled_reader_v1_parse_text_ids(
        context, text, universe, out_ids, &receipt,
        error_buf, error_buf_size);
}

static int main_petta_compiled_file_backend(
    void *context, const char *filename, TermUniverse *universe,
    AtomId **out_ids, char *error_buf, size_t error_buf_size) {
    PeTTaCompiledReaderV1Receipt receipt;
    return petta_compiled_reader_v1_parse_file_ids(
        context, filename, universe, out_ids, &receipt,
        error_buf, error_buf_size);
}

static int main_prime_compiled_text_backend(
    void *context, const char *text, TermUniverse *universe,
    AtomId **out_ids, char *error_buf, size_t error_buf_size) {
    PrimeCompiledReaderV1Receipt receipt;
    return prime_compiled_reader_v1_parse_text_ids(
        context, text, universe, out_ids, &receipt,
        error_buf, error_buf_size);
}

static int main_prime_compiled_file_backend(
    void *context, const char *filename, TermUniverse *universe,
    AtomId **out_ids, char *error_buf, size_t error_buf_size) {
    PrimeCompiledReaderV1Receipt receipt;
    return prime_compiled_reader_v1_parse_file_ids(
        context, filename, universe, out_ids, &receipt,
        error_buf, error_buf_size);
}

static void main_he_compiled_reader_free(void *context) {
    he_compiled_reader_v1_free(context);
}

static void main_petta_compiled_reader_free(void *context) {
    petta_compiled_reader_v1_free(context);
}

static void main_prime_compiled_reader_free(void *context) {
    prime_compiled_reader_v1_free(context);
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
    bool parser_rational_literals_set;
    bool parser_rational_literals_old;
    bool parser_universal_names_set;
    bool parser_universal_names_old;
    void *document_reader_context;
    void (*document_reader_free)(void *context);
    CettaGsltLanguage *gslt_language;
    CettaGsltRevisionedSpaceProviderV1 *gslt_revisioned_space_provider;
    CettaGsltAbtProviderV1 *gslt_abt_provider;
    CettaGsltOwnedProviderRegistryV1 gslt_physical_providers;
} CettaMainCleanup;

static void cetta_main_cleanup_registry_spaces(
    Registry *registry, Space *root_space, PettaProgram *petta_program) {
    if (!registry || !root_space) return;
    for (uint32_t ri = 0; ri < registry->len; ri++) {
        Atom *val = registry->entries[ri].value;
        if (!val) continue;
        if (val->kind == ATOM_GROUNDED && val->ground.gkind == GV_SPACE) {
            Space *sp = (Space *)val->ground.ptr;
            if (sp != root_space) {
                bool already_freed = false;
                for (uint32_t previous = 0; previous < ri; previous++) {
                    Atom *prior = registry->entries[previous].value;
                    if (prior &&
                        prior->kind == ATOM_GROUNDED &&
                        prior->ground.gkind == GV_SPACE &&
                        (Space *)prior->ground.ptr == sp) {
                        already_freed = true;
                        break;
                    }
                }
                if (already_freed)
                    continue;
                if (petta_program)
                    petta_program_forget_space(
                        petta_program, sp);
                space_free(sp);
            }
        }
    }
}

static void cetta_main_cleanup(CettaMainCleanup *cleanup) {
    if (!cleanup) return;

    if (cleanup->parser_rational_literals_set) {
        parser_set_rational_literals_enabled(cleanup->parser_rational_literals_old);
        cleanup->parser_rational_literals_set = false;
    }
    if (cleanup->parser_universal_names_set) {
        parser_set_universal_name_syntax_enabled(
            cleanup->parser_universal_names_old);
        cleanup->parser_universal_names_set = false;
    }

    if (cleanup->output_spool) {
        fclose(cleanup->output_spool);
        cleanup->output_spool = NULL;
    }

    free(cleanup->atoms);
    cleanup->atoms = NULL;
    free(cleanup->atom_ids);
    cleanup->atom_ids = NULL;
    parser_clear_document_ids_backend(cleanup->document_reader_context);
    if (cleanup->document_reader_free)
        cleanup->document_reader_free(cleanup->document_reader_context);
    cleanup->document_reader_context = NULL;
    cleanup->document_reader_free = NULL;
    cetta_gslt_owned_provider_registry_free_v1(
        &cleanup->gslt_physical_providers);
    cetta_gslt_revisioned_space_provider_free_v1(
        cleanup->gslt_revisioned_space_provider);
    cleanup->gslt_revisioned_space_provider = NULL;
    cetta_gslt_abt_provider_free_v1(cleanup->gslt_abt_provider);
    cleanup->gslt_abt_provider = NULL;
    cetta_gslt_language_free(cleanup->gslt_language);
    cleanup->gslt_language = NULL;

    if (cleanup->registry_initialized) {
        if (cleanup->space_initialized) {
            cetta_main_cleanup_registry_spaces(
                cleanup->registry, cleanup->space,
                cleanup->libraries_initialized
                    ? cleanup->libraries->petta_program
                    : NULL);
            eval_cleanup_owned_new_spaces(cleanup->registry, cleanup->space);
        } else {
            eval_cleanup_owned_new_spaces(NULL, NULL);
        }
        registry_free(cleanup->registry);
        cleanup->registry_initialized = false;
    } else {
        eval_cleanup_owned_new_spaces(NULL, NULL);
    }

    if (cleanup->libraries_initialized) {
        cetta_library_context_free(cleanup->libraries);
        cleanup->libraries_initialized = false;
    }
    cetta_lib_prolog_global_shutdown();
    cetta_foreign_global_shutdown();
    eval_match_decision_cache_free_for_current_thread();
    eval_profiled_type_cache_free_for_current_thread();

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

static bool main_document_exec_at(
    const TermUniverse *universe, const AtomId *atom_ids,
    int atom_count, int index, AtomId *payload, int *width) {
    if (payload)
        *payload = CETTA_ATOM_ID_NONE;
    if (width)
        *width = 0;
    if (!universe || !atom_ids || index < 0 ||
        index >= atom_count) {
        return false;
    }
    if (atom_id_is_symbol_id(
            universe, atom_ids[index], g_builtin_syms.bang) &&
        index + 1 < atom_count) {
        if (payload)
            *payload = atom_ids[index + 1];
        if (width)
            *width = 2;
        return true;
    }
    AtomId expanded_payload = CETTA_ATOM_ID_NONE;
    if (parser_universal_name_syntax_enabled() &&
        parser_syn_exec_payload_id(
            universe, atom_ids[index], &expanded_payload)) {
        if (payload)
            *payload = expanded_payload;
        if (width)
            *width = 1;
        return true;
    }
    return false;
}

typedef enum {
    MAIN_PETTA_BLOCK_LOAD_FAILED = 0,
    MAIN_PETTA_BLOCK_LOAD_OK,
    MAIN_PETTA_BLOCK_LOAD_TYPE_REJECTED,
} MainPettaBlockLoadResult;

static MainPettaBlockLoadResult main_petta_check_forms(
    PettaProgram *program, Space *space, Registry *registry,
    TermUniverse *universe, AtomId *atom_ids, int atom_count,
    PettaTypecheckPolicy typecheck_policy) {
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
    if (atom_count <= 0)
        return MAIN_PETTA_BLOCK_LOAD_OK;
    Atom **forms = cetta_malloc(
        sizeof(*forms) * (size_t)atom_count);
    if (!forms)
        return MAIN_PETTA_BLOCK_LOAD_FAILED;
    for (int index = 0; index < atom_count; index++)
        forms[index] = term_universe_get_atom(universe, atom_ids[index]);
    PettaTypecheckBlockResult checked;
    bool judged = petta_typecheck_declaration_block_selected(
        program, space, registry, forms, (size_t)atom_count,
        typecheck_policy, &checked);
    free(forms);
    if (!judged) {
        fprintf(
            stderr, "PeTTa typechecker fault: %s\n",
            checked.diagnostic[0]
                ? checked.diagnostic : "declaration analysis failed");
        return MAIN_PETTA_BLOCK_LOAD_FAILED;
    }
    if (checked.verdict == PETTA_TYPECHECK_REFUTED) {
        fprintf(
            stderr, "PeTTa type error: %s\n",
            checked.diagnostic[0]
                ? checked.diagnostic : "declaration block rejected");
        return MAIN_PETTA_BLOCK_LOAD_TYPE_REJECTED;
    }
    return MAIN_PETTA_BLOCK_LOAD_OK;
#else
    (void)program;
    (void)space;
    (void)registry;
    (void)universe;
    (void)atom_ids;
    (void)atom_count;
    (void)typecheck_policy;
    return MAIN_PETTA_BLOCK_LOAD_OK;
#endif
}

static MainPettaBlockLoadResult main_petta_load_declaration_block(
    PettaProgram *program, Space *space, Registry *registry,
    TermUniverse *universe, AtomId *atom_ids,
    int atom_count, bool typecheck,
    PettaTypecheckPolicy typecheck_policy) {
    if (typecheck && atom_count > 0) {
        MainPettaBlockLoadResult checked = main_petta_check_forms(
            program, space, registry, universe, atom_ids, atom_count,
            typecheck_policy);
        if (checked != MAIN_PETTA_BLOCK_LOAD_OK)
            return checked;
    }
    cetta_petta_erase_typecheck_marks_document(
        universe, atom_ids, atom_count);
    PettaDeclarationBlock *block =
        petta_program_declaration_block_new(
            program, universe, atom_ids, atom_count);
    if (!block)
        return MAIN_PETTA_BLOCK_LOAD_FAILED;
    bool ok = true;
    for (int index = 0; ok && index < atom_count; index++) {
        Atom *source =
            term_universe_get_atom(universe, atom_ids[index]);
        const PettaPlanNode *plan =
            petta_program_declaration_block_plan_at(
                block, index);
        if (!source || !plan) {
            ok = false;
            break;
        }
        space_add_atom_id(space, atom_ids[index]);
        ok = petta_program_note_add(
            program, space, source, plan);
    }
    if (ok && typecheck) {
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
        petta_typecheck_inferred_signatures_rebase_selected(
            program, space, typecheck_policy);
#else
        petta_program_inferred_signatures_rebase(program, space);
#endif
    }
    petta_program_declaration_block_free(block);
    return ok ? MAIN_PETTA_BLOCK_LOAD_OK
              : MAIN_PETTA_BLOCK_LOAD_FAILED;
}

static bool main_load_planned_declaration_block(
    PettaProgram *program, Space *space,
    TermUniverse *universe, AtomId *atom_ids, int atom_count) {
    if (!program || !space || !universe || atom_count < 0 ||
        (atom_count > 0 && !atom_ids)) {
        return false;
    }
    PettaDeclarationBlock *block =
        petta_program_declaration_block_new(
            program, universe, atom_ids, atom_count);
    if (!block) return false;
    bool ok = space_add_atom_ids_batch(
        space, atom_ids, (CettaCount)atom_count);
    for (int index = 0; ok && index < atom_count; index++) {
        Atom *source = term_universe_get_atom(
            universe, atom_ids[index]);
        const PettaPlanNode *plan =
            petta_program_declaration_block_plan_at(block, index);
        ok = source && plan &&
             petta_program_note_add(program, space, source, plan);
    }
    petta_program_declaration_block_free(block);
    return ok;
}

static int main_run_langdef_source(const char *manifest_path,
                                   const char *filename,
                                   const char *inline_text,
                                   CettaLangDefProofExecutionV1 proof_execution,
                                   bool emit_runtime_stats) {
    Arena arena;
    SymbolTable symbol_table;
    VarInternTable var_intern_table;
    HashConsTable hashcons_table;
    CettaLangDefRunReceiptV1 receipt = {
        .status = CETTA_LANGDEF_RUN_V1_ERROR,
        .result = NULL,
    };
    char error[512] = {0};
    bool ok;
    int rc;

    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    symbol_table_init(&symbol_table);
    symbol_table_init_builtins(&symbol_table, &g_builtin_syms);
    g_symbols = &symbol_table;
    var_intern_init(&var_intern_table);
    g_var_intern = &var_intern_table;
    hashcons_init(&hashcons_table);
    g_hashcons = &hashcons_table;
    arena_set_hashcons(&arena, &hashcons_table);

    if (emit_runtime_stats) {
        cetta_runtime_stats_reset();
        cetta_runtime_stats_enable();
    }
    ok = inline_text
        ? cetta_langdef_run_bytes_with_proof_execution_v1(
              manifest_path, (const uint8_t *)inline_text,
              strlen(inline_text), NULL, proof_execution,
              &arena, &receipt,
              error, sizeof(error))
        : cetta_langdef_run_file_with_proof_execution_v1(
              manifest_path, filename, proof_execution,
              &arena, &receipt,
              error, sizeof(error));
    if (!ok) {
        fprintf(stderr, "error: %s\n",
                error[0] ? error : "language-definition execution failed");
        rc = 1;
    } else {
        FILE *output =
            receipt.status == CETTA_LANGDEF_RUN_V1_ACCEPTED ||
                    receipt.status ==
                        CETTA_LANGDEF_RUN_V1_ACCEPTED_INCOMPLETE
            ? stdout : stderr;
        atom_print(receipt.result, output);
        fputc('\n', output);
        switch (receipt.status) {
        case CETTA_LANGDEF_RUN_V1_ACCEPTED:
        case CETTA_LANGDEF_RUN_V1_ACCEPTED_INCOMPLETE:
            rc = 0;
            break;
        case CETTA_LANGDEF_RUN_V1_REJECTED:
            rc = 2;
            break;
        case CETTA_LANGDEF_RUN_V1_INCOMPLETE:
            rc = 3;
            break;
        case CETTA_LANGDEF_RUN_V1_UNSUPPORTED:
            rc = 4;
            break;
        case CETTA_LANGDEF_RUN_V1_ERROR:
        default:
            rc = 1;
            break;
        }
    }
    if (emit_runtime_stats) {
        CettaRuntimeStats stats;
        cetta_runtime_stats_snapshot(&stats);
        cetta_runtime_stats_print(stderr, &stats);
    }

    g_hashcons = NULL;
    g_var_intern = NULL;
    g_symbols = NULL;
    hashcons_free(&hashcons_table);
    var_intern_free(&var_intern_table);
    symbol_table_free(&symbol_table);
    arena_free(&arena);
    return rc;
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

    const CettaProfile *profile = NULL;
    CettaCliEndpoint source_endpoint;
    CettaCliEndpoint target_endpoint;
    CettaCliEndpoint *current_endpoint = NULL;
    bool import_mode_overridden = false;
    CettaRelativeModulePolicy import_mode = CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY;
    const char *filename = NULL;
    const char *inline_text = NULL;
    char *inline_buf = NULL;
    size_t inline_len = 0;
    size_t inline_cap = 0;
    const char *script_path = NULL;
    int script_arg_start = -1;
    int filename_arg_index = -1;
    int petta_file_arg_cursor = -1;
    int petta_file_arg_end = -1;
    bool compile_mode = false;
    bool compile_stdlib_mode = false;
    bool count_only = false;
    bool emit_runtime_stats = false;
    CettaLangDefProofExecutionV1 langdef_proof_execution =
        CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY;
    bool langdef_proof_execution_requested = false;
    bool emit_prime_need_trace = false;
    bool prime_need_trace_failed = false;
    bool petta_strict = false;
    bool petta_strict_det = false;
    const char *prime_rewrite_frontier = NULL;
    bool eval_hashcons = false;
    bool list_profiles = false;
    bool translate_mode = false;
    uint32_t lang_occurrences = 0;
    bool prefer_rationals_cli = false;
    int fuel_override = -1;
    uint32_t num_threads = 1u;
    bool num_threads_requested = false;
    uint32_t rho_reduction_limit = CETTA_RHOCALC_DEFAULT_REDUCTION_LIMIT;
    bool rho_reduction_limit_requested = false;
    RhoSchedulerPolicy rho_scheduler = RHO_SCHEDULER_CANONICAL;
    bool rho_scheduler_requested = false;
    uint64_t mm2_step_limit = CETTA_MM2_DEFAULT_RUN_STEPS;
    SpaceEngine space_engine = SPACE_ENGINE_NATIVE;
    bool space_engine_requested = false;
    CettaGsltRealization gslt_realization =
        CETTA_GSLT_REALIZATION_HORN_REFERENCE;
    bool gslt_realization_requested = false;

    endpoint_init(&source_endpoint, "he");
    endpoint_init(&target_endpoint, NULL);
    current_endpoint = &source_endpoint;

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
            list_profiles = true;
            continue;
        }
        if (strcmp(argv[i], "--translate") == 0 || strcmp(argv[i], "-t") == 0) {
            translate_mode = true;
            continue;
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
        if (strcmp(argv[i], "--langdef-proof-backend") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            const char *value = argv[++i];
            if (strcmp(value, "authority") == 0) {
                langdef_proof_execution =
                    CETTA_LANGDEF_PROOF_EXECUTION_V1_AUTHORITY;
#if CETTA_BUILD_WITH_LANGDEF_DIAGNOSTIC_BACKENDS
            } else if (strcmp(
                           value, "frame-cache-diagnostic-v1") == 0) {
                langdef_proof_execution =
                    CETTA_LANGDEF_PROOF_EXECUTION_V1_FRAME_CACHE_DIAGNOSTIC;
#endif
            } else if (strcmp(
                           value, "generated-relational-audit-v1") == 0) {
                langdef_proof_execution =
                    CETTA_LANGDEF_PROOF_EXECUTION_V1_GENERATED_RELATIONAL_AUDIT;
            } else {
                fprintf(stderr,
                        "error: unknown langdef proof backend '%s'\n",
                        value);
                return 2;
            }
            langdef_proof_execution_requested = true;
            continue;
        }
        if (strcmp(argv[i], "--emit-prime-need-trace") == 0) {
            emit_prime_need_trace = true;
            continue;
        }
        if (strcmp(argv[i], "--strict") == 0) {
            petta_strict = true;
            continue;
        }
        if (strcmp(argv[i], "--strict-det") == 0) {
            petta_strict = true;
            petta_strict_det = true;
            continue;
        }
        if (strcmp(argv[i], "--prime-rewrite-frontier") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            const char *value = argv[++i];
            if (strcmp(value, "monolithic") != 0 &&
                strcmp(value, "candidate-local") != 0 &&
                strcmp(value, "demand-cohort") != 0) {
                fprintf(stderr,
                        "error: invalid Prime rewrite frontier '%s'\n",
                        value);
                return 2;
            }
            prime_rewrite_frontier = value;
            continue;
        }
        if (strcmp(argv[i], "--eval-hashcons") == 0) {
            eval_hashcons = true;
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
        if (strcmp(argv[i], "--prefer-rationals") == 0) {
            prefer_rationals_cli = true;
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
        if (strcmp(argv[i], "--num-threads") == 0) {
            char *endp = NULL;
            unsigned long long parsed;
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            parsed = strtoull(argv[++i], &endp, 10);
            if (!endp || *endp != '\0' || parsed > 1024ULL) {
                fprintf(stderr, "error: invalid thread count '%s'\n", argv[i]);
                return 2;
            }
            num_threads = (uint32_t)parsed;
            num_threads_requested = true;
            continue;
        }
        if (strcmp(argv[i], "--rho-reduction-limit") == 0) {
            char *endp = NULL;
            unsigned long long parsed;
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            parsed = strtoull(argv[++i], &endp, 10);
            if (!endp || *endp != '\0' || parsed == 0 ||
                parsed > 100000000ULL) {
                fprintf(stderr, "error: invalid rhocalc reduction limit '%s'\n", argv[i]);
                return 2;
            }
            rho_reduction_limit = (uint32_t)parsed;
            rho_reduction_limit_requested = true;
            continue;
        }
        if (strcmp(argv[i], "--rho-scheduler") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            if (!rho_scheduler_policy_from_name(argv[++i], &rho_scheduler)) {
                fprintf(stderr, "error: unknown rho scheduler '%s'\n", argv[i]);
                return 2;
            }
            rho_scheduler_requested = true;
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
            space_engine_requested = true;
            continue;
        }
        if (strcmp(argv[i], "--gslt-realization") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            if (!cetta_gslt_realization_parse(
                    argv[++i], &gslt_realization)) {
                fprintf(stderr, "error: unknown GSLT realization '%s'\n",
                        argv[i]);
                return 2;
            }
            gslt_realization_requested = true;
            continue;
        }
        if (strcmp(argv[i], "--lang") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            if (translate_mode && lang_occurrences >= 1) {
                target_endpoint.lang_name = argv[++i];
                current_endpoint = &target_endpoint;
            } else {
                source_endpoint.lang_name = argv[++i];
                current_endpoint = &source_endpoint;
            }
            lang_occurrences++;
            if (translate_mode && lang_occurrences > 2) {
                fprintf(stderr, "error: --translate accepts at most two --lang endpoints\n");
                return 2;
            }
            if (!translate_mode && lang_occurrences > 1) {
                fprintf(stderr, "error: repeated --lang requires --translate\n");
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "--syntax") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            if (!cetta_syntax_from_name(argv[++i], &current_endpoint->syntax)) {
                fprintf(stderr, "error: unknown syntax '%s'\n", argv[i]);
                return 2;
            }
            continue;
        }
        if (strcmp(argv[i], "--profile") == 0) {
            if (i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            current_endpoint->profile_name = argv[++i];
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
            import_mode_overridden = true;
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
            filename_arg_index = i;
            script_path = filename;
            script_arg_start = i + 1;
            break;
        }
    }

    if (inline_buf)
        inline_text = inline_buf;

    if (translate_mode && !target_endpoint.lang_name) {
        target_endpoint = source_endpoint;
    }
    if (!endpoint_resolve(&source_endpoint, "source", argv[0])) {
        free(inline_buf);
        return 2;
    }
    if (translate_mode &&
        !endpoint_resolve(&target_endpoint, "target", argv[0])) {
        free(inline_buf);
        return 2;
    }
    if (translate_mode && target_endpoint.langdef_manifest_path[0]) {
        fprintf(stderr,
                "error: language-definition endpoints do not support translation yet\n");
        free(inline_buf);
        return 2;
    }

    if (source_endpoint.langdef_manifest_path[0]) {
        bool unsupported_option =
            translate_mode || compile_mode || compile_stdlib_mode ||
            list_profiles || source_endpoint.syntax != CETTA_SYNTAX_AUTO ||
            import_mode_overridden || petta_strict || petta_strict_det ||
            emit_prime_need_trace || prime_rewrite_frontier ||
            prefer_rationals_cli || eval_hashcons || fuel_override > 0 ||
            num_threads_requested || rho_reduction_limit_requested ||
            rho_scheduler_requested ||
            mm2_step_limit != CETTA_MM2_DEFAULT_RUN_STEPS ||
            space_engine != SPACE_ENGINE_NATIVE;
        if (unsupported_option) {
            fprintf(stderr,
                    "error: selected language definition supports direct source execution only\n");
            free(inline_buf);
            return 2;
        }
        if (!filename && !inline_text) {
            print_usage(stderr);
            free(inline_buf);
            return 1;
        }
        int langdef_rc = main_run_langdef_source(
            source_endpoint.langdef_manifest_path,
            filename, inline_text, langdef_proof_execution,
            emit_runtime_stats);
        free(inline_buf);
        return langdef_rc;
    }

    if (langdef_proof_execution_requested) {
        fprintf(stderr,
                "error: --langdef-proof-backend requires a loaded language definition\n");
        free(inline_buf);
        return 2;
    }

    const CettaLanguageSpec *lang = source_endpoint.lang;
    profile = source_endpoint.profile;
    if (lang->id == CETTA_LANGUAGE_PETTA && filename && !inline_text) {
        petta_file_arg_cursor = filename_arg_index;
        petta_file_arg_end = filename_arg_index + 1;
        while (petta_file_arg_end < argc &&
               path_has_suffix(argv[petta_file_arg_end], ".metta")) {
            petta_file_arg_end++;
        }
        script_arg_start = petta_file_arg_end;
    }
    if (gslt_realization_requested &&
        !cetta_language_uses_embedded_gslt(source_endpoint.lang->id)) {
        fprintf(stderr,
                "error: --gslt-realization requires an embedded generated "
                "GSLT language (zero, subzero, gslt-il, zerouv, or metta-interact)\n");
        return 2;
    }

    if ((petta_strict || petta_strict_det) &&
        (lang->id != CETTA_LANGUAGE_PETTA || !profile ||
         profile->id != CETTA_PROFILE_PETTA_TYPECHECK_V2)) {
        fprintf(
            stderr,
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
            "error: --strict and --strict-det require --lang petta --profile typecheck-v2\n");
#else
            "error: --strict and --strict-det are not valid for the selected language profile\n");
#endif
        free(inline_buf);
        return 2;
    }
    PettaTypecheckPolicy petta_typecheck_policy = petta_strict_det
        ? PETTA_TYPECHECK_POLICY_STRICT_DET
        : petta_strict
            ? PETTA_TYPECHECK_POLICY_STRICT
            : PETTA_TYPECHECK_POLICY_DEFAULT;

    if (emit_prime_need_trace && lang->id != CETTA_LANGUAGE_PRIME) {
        fprintf(stderr,
                "error: --emit-prime-need-trace requires --lang prime\n");
        free(inline_buf);
        return 2;
    }
#if !CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (emit_prime_need_trace) {
        fprintf(stderr,
                "error: --emit-prime-need-trace requires a causal-receipt build\n");
        free(inline_buf);
        return 2;
    }
#endif
    if (prime_rewrite_frontier && lang->id != CETTA_LANGUAGE_PRIME) {
        fprintf(stderr,
                "error: --prime-rewrite-frontier requires --lang prime\n");
        free(inline_buf);
        return 2;
    }

    if (list_profiles) {
        cetta_profile_print_inventory_for_language(stdout, lang->id);
        free(inline_buf);
        return 0;
    }

    if (!filename && !inline_text) {
        print_usage(stderr);
        free(inline_buf);
        return 1;
    }

    CettaSyntaxId syntax = endpoint_effective_syntax(&source_endpoint, filename);
    if (!endpoint_supports_syntax(&source_endpoint, syntax, "source")) {
        free(inline_buf);
        return 2;
    }

    if (translate_mode) {
        RhocalcSemanticProfileId input_profile =
            rhocalc_semantic_profile_for_endpoint(&source_endpoint);
        RhocalcSemanticProfileId output_profile =
            rhocalc_semantic_profile_for_endpoint(&target_endpoint);
        if (rho_reduction_limit_requested || rho_scheduler_requested ||
            num_threads_requested) {
            fprintf(stderr, "error: runtime thread/reduction flags do not combine with --translate\n");
            free(inline_buf);
            return 2;
        }
        CettaSyntaxId output_syntax =
            endpoint_effective_syntax(&target_endpoint, NULL);
        if (!endpoint_supports_syntax(&target_endpoint, output_syntax, "target")) {
            free(inline_buf);
            return 2;
        }
        if (compile_mode || compile_stdlib_mode) {
            fprintf(stderr, "error: --translate does not combine with compile modes\n");
            free(inline_buf);
            return 2;
        }
        if (source_endpoint.lang->id != CETTA_LANGUAGE_RHOCALC ||
            target_endpoint.lang->id != CETTA_LANGUAGE_RHOCALC) {
            fprintf(stderr, "error: --translate currently supports only rhocalc endpoints\n");
            free(inline_buf);
            return 2;
        }
        int translate_rc =
            run_rhocalc_translation(filename, inline_text,
                                    input_profile, output_profile,
                                    syntax, output_syntax);
        free(inline_buf);
        return translate_rc;
    }

    if (lang->id == CETTA_LANGUAGE_RHOCALC) {
        RhocalcSemanticProfileId semantic_profile =
            rhocalc_semantic_profile_for_endpoint(&source_endpoint);
        bool rho_threaded = num_threads_requested && num_threads > 1u;
        if (rho_threaded && rho_scheduler_requested) {
            fprintf(stderr,
                    "error: --rho-scheduler does not combine with --num-threads > 1\n");
            free(inline_buf);
            return 2;
        }
        RhoRuntimeProfile rho_profile = {
            .scheduler_policy = rho_scheduler,
            .reduction_limit = rho_reduction_limit,
            .thread_count = rho_threaded ? num_threads : 1u,
            .threaded = rho_threaded,
        };
        if (compile_mode || compile_stdlib_mode) {
            fprintf(stderr, "error: compile modes are not supported with --lang rhocalc\n");
            free(inline_buf);
            return 2;
        }
        if (count_only) {
            fprintf(stderr,
                    "error: --count-only is not supported with --lang rhocalc\n");
            free(inline_buf);
            return 2;
        }
        if (emit_runtime_stats) {
            cetta_runtime_stats_reset();
            cetta_runtime_stats_enable();
        }
        int rho_rc = run_rhocalc_cli(filename, inline_text, semantic_profile,
                                     syntax, &rho_profile);
        if (emit_runtime_stats) {
            CettaRuntimeStats stats;
            cetta_runtime_stats_snapshot(&stats);
            cetta_runtime_stats_print(stderr, &stats);
        }
        free(inline_buf);
        return rho_rc;
    }

    if (rho_reduction_limit_requested || rho_scheduler_requested) {
        fprintf(stderr, "error: rhocalc runtime flags require --lang rhocalc\n");
        free(inline_buf);
        return 2;
    }

    bool mm2_gslt_profile =
        profile && profile->id == CETTA_PROFILE_MM2_GSLT;
    if (mm2_gslt_profile && (compile_mode || compile_stdlib_mode)) {
        fprintf(stderr,
                "error: compile modes are not declared observations of "
                "--lang mm2 --profile gslt\n");
        free(inline_buf);
        return 2;
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

    if (strcmp(lang->canonical, "mm2") != 0 &&
        mm2_step_limit != CETTA_MM2_DEFAULT_RUN_STEPS) {
        fprintf(stderr, "error: --steps is currently only supported with --lang mm2\n");
        return 2;
    }

    if (!compile_mode && strcmp(lang->canonical, "mm2") == 0 &&
        !mm2_gslt_profile &&
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
    HECompiledReaderV1 *he_compiled_reader = NULL;
    PeTTaCompiledReaderV1 *petta_compiled_reader = NULL;
    PrimeCompiledReaderV1 *prime_compiled_reader = NULL;
    CettaGsltLanguage *gslt_language = NULL;
    CettaMainCleanup cleanup = {0};
    bool lang_is_mm2 = strcmp(lang->canonical, "mm2") == 0;
    char document_reader_error[512] = {0};
    int n = 0;

    g_count_only = count_only;
    if (fuel_override > 0) eval_set_default_fuel(fuel_override);

    cleanup.inline_buf = inline_buf;
    cleanup.arena = &arena;
    cleanup.eval_arena = &eval_arena;
    cleanup.symbol_table = &symbol_table;
    cleanup.var_intern_table = &var_intern_table;
    cleanup.hashcons_table = &hashcons_table;
    cleanup.document_reader_context = NULL;
    cleanup.document_reader_free = NULL;
    cleanup.gslt_language = NULL;
    cleanup.gslt_revisioned_space_provider = NULL;
    cleanup.gslt_abt_provider = NULL;
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
    arena_set_hashcons(&eval_arena, eval_hashcons ? &hashcons_table : NULL);

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
        if (!mm2_gslt_profile)
            cetta_mm2_lower_atoms(&arena, atoms, n);
    }
    if (emit_runtime_stats) {
        cetta_runtime_stats_reset();
        cetta_runtime_stats_enable();
    }
    if (lang_is_mm2 && mm2_gslt_profile) {
        CettaMm2GsltRealization mm2_realization =
            CETTA_MM2_GSLT_REALIZATION_AUTO;
        if (space_engine_requested) {
            if (space_engine == SPACE_ENGINE_NATIVE) {
                mm2_realization = CETTA_MM2_GSLT_REALIZATION_NATIVE_C;
            } else if (space_engine == SPACE_ENGINE_PATHMAP) {
                mm2_realization =
                    CETTA_MM2_GSLT_REALIZATION_RUST_PATHMAP_ABI;
            } else {
                fprintf(stderr,
                        "error: MM2 GSLT supports only native or pathmap "
                        "space engines\n");
                rc = 2;
                goto cleanup;
            }
        }
        int mm2_rc = run_mm2_program_via_gslt(
            &arena, atoms, n, count_only, mm2_step_limit,
            mm2_realization);
        if (emit_runtime_stats) {
            CettaRuntimeStats stats;
            cetta_runtime_stats_snapshot(&stats);
            cetta_runtime_stats_print(stderr, &stats);
        }
        rc = mm2_rc;
        goto cleanup;
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

    cetta_library_context_init_for_language_profile(&libraries, lang->id, profile);
    cleanup.libraries_initialized = true;
    term_universe_set_persistent_arena(&libraries.term_universe, &arena);
    cetta_library_context_set_exec_path(&libraries, argv[0]);
    cetta_library_context_set_script_path(&libraries, script_path);
    cetta_library_context_set_cli_args(&libraries, argc, argv, script_arg_start);
    if (profile &&
        profile->id == CETTA_PROFILE_PETTA_TYPECHECK_V2) {
        const char *policy_name = petta_typecheck_policy ==
                PETTA_TYPECHECK_POLICY_STRICT_DET
            ? "strict-det"
            : petta_typecheck_policy == PETTA_TYPECHECK_POLICY_STRICT
                ? "strict" : "default";
        if (!cetta_eval_session_record_generic_setting(
                &libraries.session, "petta-typecheck-policy",
                CETTA_EVAL_OPTION_VALUE_SYMBOL, policy_name, 0)) {
            fprintf(stderr, "error: could not configure PeTTa typechecking\n");
            rc = 1;
            goto cleanup;
        }
    }
    if (import_mode_overridden) {
        cetta_eval_session_set_relative_module_policy(&libraries.session, import_mode);
    }
    if (prefer_rationals_cli) {
        cetta_eval_session_record_generic_setting(
            &libraries.session, "prefer-rationals",
            CETTA_EVAL_OPTION_VALUE_SYMBOL, "true", 0);
    }
    if (num_threads_requested) {
        char repr[32];
        snprintf(repr, sizeof(repr), "%u", num_threads);
        cetta_eval_session_record_generic_setting(
            &libraries.session, "num-threads",
            CETTA_EVAL_OPTION_VALUE_INT, repr, (int64_t)num_threads);
    }
    if (prime_rewrite_frontier) {
        cetta_eval_session_record_generic_setting(
            &libraries.session, "prime-rewrite-frontier",
            CETTA_EVAL_OPTION_VALUE_SYMBOL, prime_rewrite_frontier, 0);
    }
    eval_set_library_context(&libraries);
    cleanup.parser_rational_literals_old =
        parser_set_rational_literals_enabled(
            !cetta_language_uses_rust_he_compat_semantics(lang->id, profile));
    cleanup.parser_rational_literals_set = true;
    cleanup.parser_universal_names_old =
        parser_set_universal_name_syntax_enabled(
            lang->id == CETTA_LANGUAGE_PRIME);
    cleanup.parser_universal_names_set = true;

    const char *document_reader_capability = NULL;
    if (lang->id == CETTA_LANGUAGE_HE)
        document_reader_capability = "he-reader-direct-v1";
    else if (lang->id == CETTA_LANGUAGE_PETTA)
        document_reader_capability = "petta-reader-direct-v1";
    else if (lang->id == CETTA_LANGUAGE_PRIME)
        document_reader_capability = "prime-reader-direct-v1";
    else if (cetta_language_uses_embedded_gslt(lang->id)) {
        char language_error[512] = {0};
        const CettaGsltEmbeddedLanguageV1 *descriptor =
            embedded_gslt_descriptor(lang->id, profile);
        if (!descriptor ||
            !cetta_gslt_language_load_embedded_for_realization(
                descriptor, gslt_realization,
                &gslt_language,
                language_error, sizeof(language_error)) ||
            strcmp(cetta_gslt_language_name(gslt_language),
                   lang->canonical) != 0) {
            fprintf(stderr,
                    "error: could not load generated language pack: %s\n",
                    language_error[0] ? language_error
                                      : "language identity mismatch");
            rc = 1;
            goto cleanup;
        }
        cleanup.gslt_language = gslt_language;
        document_reader_capability =
            cetta_gslt_language_syntax_backend(gslt_language);
    }

    if (document_reader_capability &&
        strcmp(document_reader_capability, "he-reader-direct-v1") == 0) {
        char reader_error[512] = {0};
        ParserDocumentIdsBackend reader_backend;
        he_compiled_reader = he_compiled_reader_v1_new();
        cleanup.document_reader_context = he_compiled_reader;
        cleanup.document_reader_free = main_he_compiled_reader_free;
        if (!he_compiled_reader ||
            !he_compiled_reader_v1_prepare(
                he_compiled_reader, reader_error, sizeof(reader_error))) {
            fprintf(stderr, "error: could not prepare compiled HE reader: %s\n",
                    reader_error[0] ? reader_error : "unknown failure");
            rc = 1;
            goto cleanup;
        }
        reader_backend = (ParserDocumentIdsBackend){
            .context = he_compiled_reader,
            .parse_text_ids = main_he_compiled_text_backend,
            .parse_file_ids = main_he_compiled_file_backend,
        };
        if (!parser_set_document_ids_backend(&reader_backend)) {
            fprintf(stderr,
                    "error: could not install compiled HE document reader\n");
            rc = 1;
            goto cleanup;
        }
    } else if (document_reader_capability &&
               strcmp(document_reader_capability,
                      "petta-reader-direct-v1") == 0) {
        char reader_error[512] = {0};
        ParserDocumentIdsBackend reader_backend;
        petta_compiled_reader = petta_compiled_reader_v1_new();
        cleanup.document_reader_context = petta_compiled_reader;
        cleanup.document_reader_free = main_petta_compiled_reader_free;
        if (!petta_compiled_reader ||
            !petta_compiled_reader_v1_prepare(
                petta_compiled_reader, reader_error,
                sizeof(reader_error))) {
            fprintf(stderr,
                    "error: could not prepare compiled PeTTa reader: %s\n",
                    reader_error[0] ? reader_error : "unknown failure");
            rc = 1;
            goto cleanup;
        }
        reader_backend = (ParserDocumentIdsBackend){
            .context = petta_compiled_reader,
            .parse_text_ids = main_petta_compiled_text_backend,
            .parse_file_ids = main_petta_compiled_file_backend,
        };
        if (!parser_set_document_ids_backend(&reader_backend)) {
            fprintf(stderr,
                    "error: could not install compiled PeTTa document reader\n");
            rc = 1;
            goto cleanup;
        }
    } else if (document_reader_capability &&
               strcmp(document_reader_capability,
                      "prime-reader-direct-v1") == 0) {
        char reader_error[512] = {0};
        ParserDocumentIdsBackend reader_backend;
        prime_compiled_reader = prime_compiled_reader_v1_new();
        cleanup.document_reader_context = prime_compiled_reader;
        cleanup.document_reader_free = main_prime_compiled_reader_free;
        if (!prime_compiled_reader ||
            !prime_compiled_reader_v1_prepare(
                prime_compiled_reader, reader_error,
                sizeof(reader_error))) {
            fprintf(stderr,
                    "error: could not prepare compiled Prime reader: %s\n",
                    reader_error[0] ? reader_error : "unknown failure");
            rc = 1;
            goto cleanup;
        }
        reader_backend = (ParserDocumentIdsBackend){
            .context = prime_compiled_reader,
            .parse_text_ids = main_prime_compiled_text_backend,
            .parse_file_ids = main_prime_compiled_file_backend,
        };
        if (!parser_set_document_ids_backend(&reader_backend)) {
            fprintf(stderr,
                    "error: could not install compiled Prime document reader\n");
            rc = 1;
            goto cleanup;
        }
    } else if (document_reader_capability) {
        fprintf(stderr, "error: generated language selected an unknown "
                        "reader capability '%s'\n",
                document_reader_capability);
        rc = 1;
        goto cleanup;
    }

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
    Atom *self_value = atom_space(&arena, &space);
    cetta_provenance_assert_not_transient(self_value, "main.registry.self");
    registry_bind_id(&registry, g_builtin_syms.self, self_value);

    if (!lang_is_mm2) {
        n = inline_text
            ? parse_metta_text_ids_diagnostic(
                  inline_text, &libraries.term_universe, &atom_ids,
                  document_reader_error, sizeof(document_reader_error))
            : parse_metta_file_ids_diagnostic(
                  filename, &libraries.term_universe, &atom_ids,
                  document_reader_error, sizeof(document_reader_error));
        cleanup.atom_ids = atom_ids;
        if (n < 0) {
            if (document_reader_error[0]) {
                const char *reader_name =
                    lang->id == CETTA_LANGUAGE_HE ? "HE" :
                    lang->id == CETTA_LANGUAGE_PETTA ? "PeTTa" :
                    lang->id == CETTA_LANGUAGE_PRIME ? "Prime" :
                    lang->canonical;
                fprintf(stderr, "error: compiled %s reader: %s\n",
                        reader_name, document_reader_error);
            }
            if (inline_text) {
                fprintf(stderr, "error: could not parse inline MeTTa text\n");
            } else {
                fprintf(stderr, "error: could not read %s\n", filename);
            }
            rc = 1;
            goto cleanup;
        }
    }

    if (gslt_language) {
        if (compile_mode) {
            fprintf(stderr,
                    "error: --compile is not a declared %s observation\n",
                    lang->canonical);
            rc = 2;
            goto cleanup;
        }
        Atom **forms = n > 0
            ? cetta_malloc(sizeof(*forms) * (size_t)n) : NULL;
        for (int index = 0; index < n; index++)
            forms[index] = term_universe_get_atom(
                &libraries.term_universe, atom_ids[index]);
        CettaGsltLanguageResult observed = {0};
        CettaGsltHornLimits limits = {
            .max_rule_attempts = 10000000u,
            .max_answers = 1000000u,
            .max_depth = 10000u,
        };
        char language_error[512] = {0};
        bool interact_profile = profile &&
            profile->id == CETTA_PROFILE_ZERO_INTERACT;
        if (interact_profile) {
            cleanup.gslt_revisioned_space_provider =
                cetta_gslt_revisioned_space_provider_create_v1(
                    space_engine, &CETTA_ZERO_INTERACT_SPACE_SCHEMA_V1,
                    language_error, sizeof(language_error));
            if (!cleanup.gslt_revisioned_space_provider) {
                free(forms);
                fprintf(
                    stderr,
                    "error: could not bind the revisioned space provider: %s\n",
                    language_error[0] ? language_error : "unknown failure");
                rc = 1;
                goto cleanup;
            }
            cleanup.gslt_abt_provider = cetta_gslt_abt_provider_create_v1(
                &CETTA_ZERO_INTERACT_ABT_SCHEMA_V1, NULL,
                language_error, sizeof(language_error));
            if (!cleanup.gslt_abt_provider) {
                free(forms);
                fprintf(
                    stderr,
                    "error: could not bind the ABT provider: %s\n",
                    language_error[0] ? language_error : "unknown failure");
                rc = 1;
                goto cleanup;
            }
            const CettaGsltProviderRegistryV1 *provider_registries[] = {
                cetta_gslt_revisioned_space_provider_registry_v1(
                    cleanup.gslt_revisioned_space_provider),
                cetta_gslt_abt_provider_registry_v1(
                    cleanup.gslt_abt_provider),
            };
            if (!cetta_gslt_provider_registry_union_v1(
                    provider_registries,
                    sizeof(provider_registries) /
                        sizeof(provider_registries[0]),
                    &cleanup.gslt_physical_providers,
                    language_error, sizeof(language_error))) {
                free(forms);
                fprintf(
                    stderr,
                    "error: could not compose physical providers: %s\n",
                    language_error[0] ? language_error : "unknown failure");
                rc = 1;
                goto cleanup;
            }
        }
        bool executed = interact_profile
            ? cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
                  gslt_language, gslt_realization,
                  &cetta_zero_interact_provider_catalog_v1,
                  &cleanup.gslt_physical_providers.registry,
                  forms, (size_t)n, &eval_arena, limits,
                  &observed, language_error, sizeof(language_error))
            : cetta_gslt_language_execute_atoms_with_realization(
                  gslt_language, gslt_realization,
                  forms, (size_t)n, &eval_arena, limits,
                  &observed, language_error, sizeof(language_error));
        free(forms);
        if (!executed) {
            fprintf(stderr, "error: generated language execution failed: %s\n",
                    language_error[0] ? language_error : "unknown failure");
            rc = 1;
            goto cleanup;
        }
        if (observed.outcome != CETTA_GSLT_HORN_COMPLETED) {
            fprintf(stderr,
                    "error: generated language execution is incomplete "
                    "(outcome=%u, attempts=%" PRIu64 ")\n",
                    (unsigned)observed.outcome, observed.rule_attempts);
            cetta_gslt_language_result_free(&observed);
            rc = 1;
            goto cleanup;
        }
        ResultSet visible;
        result_set_init(&visible);
        for (size_t index = 0u; index < observed.answer_count; index++)
            result_set_add(&visible, observed.answers[index]);
        if (visible.len == 0u &&
            strcmp(cetta_gslt_language_observation(gslt_language),
                   "bag") == 0)
            fputs("[]\n", stdout);
        else
            write_results(stdout, &visible, lang->id, profile);
        result_set_free(&visible);
        cetta_gslt_language_result_free(&observed);
        if (emit_runtime_stats) {
            CettaRuntimeStats stats;
            cetta_runtime_stats_snapshot(&stats);
            cetta_runtime_stats_print(stderr, &stats);
        }
        rc = 0;
        goto cleanup;
    }

    /*
     * PeTTa owns its library equations.  Loading HE's definitions into
     * &self changes clause choice, specialization, and reflection even when
     * individual helpers look similar.  The fallback is diagnostic only;
     * shared native primitives remain available through the evaluator.
     */
    if (lang->id == CETTA_LANGUAGE_PETTA) {
        const char *he_fallback =
            getenv("CETTA_PETTA_HE_STDLIB_FALLBACK");
        if (he_fallback && he_fallback[0] != '\0' &&
            strcmp(he_fallback, "0") != 0) {
            stdlib_load_petta_shared_subset(&space, &arena);
        }
    } else
        stdlib_load(&space, &arena);

    /* Add grounded op type declarations (HE stdlib implicit types) */
    if (lang->id != CETTA_LANGUAGE_PETTA)
        main_add_builtin_type_decls(&space, &arena, lang->id, profile);

    int i = 0;
    bool stop_document_sequence = false;
    FILE *output_spool = NULL;
    if (!compile_mode) {
        output_spool = tmpfile();
        if (!output_spool) {
            fprintf(stderr, "error: could not create output spool\n");
            rc = 1;
            goto cleanup;
        }
        cleanup.output_spool = output_spool;
    }

process_petta_document:

    /*
     * Upstream PeTTa's parse pass registers every top-level equation head
     * before the execution pass begins.  Record only those authored
     * declarations here; executable payloads can add heads later through the
     * ordinary dynamic-definition seam.
     */
    if (lang->id == CETTA_LANGUAGE_PETTA) {
        int declaration_index = 0;
        while (declaration_index < n) {
            int declaration_exec_width = 0;
            if (main_document_exec_at(
                    &libraries.term_universe, atom_ids, n,
                    declaration_index, NULL,
                    &declaration_exec_width)) {
                declaration_index += declaration_exec_width;
                continue;
            }
            Atom *declaration = term_universe_get_atom(
                &libraries.term_universe,
                atom_ids[declaration_index]);
            if (petta_program_is_equation(declaration) &&
                !petta_program_predeclare_equation(
                    libraries.petta_program, declaration)) {
                fprintf(
                    stderr,
                    "error: could not predeclare PeTTa equation head\n");
                rc = 1;
                goto cleanup;
            }
            declaration_index++;
        }
    }

    /* Compile mode: load all atoms into space, emit LLVM IR, exit */
    if (compile_mode) {
        if (lang_is_mm2) {
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
        } else if (lang->id == CETTA_LANGUAGE_PETTA) {
            int pi = 0;
            while (pi < n) {
                int exec_width = 0;
                if (main_document_exec_at(
                        &libraries.term_universe, atom_ids,
                        n, pi, NULL, &exec_width)) {
                    if (profile &&
                        profile->id == CETTA_PROFILE_PETTA_TYPECHECK_V2) {
                        MainPettaBlockLoadResult checked =
                            main_petta_check_forms(
                                libraries.petta_program, &space, &registry,
                                &libraries.term_universe, atom_ids + pi,
                                exec_width, petta_typecheck_policy);
                        if (checked != MAIN_PETTA_BLOCK_LOAD_OK) {
                            rc = checked ==
                                    MAIN_PETTA_BLOCK_LOAD_TYPE_REJECTED
                                ? 2 : 1;
                            goto cleanup;
                        }
                    }
                    pi += exec_width;
                    continue;
                }
                int block_end = pi + 1;
                while (block_end < n &&
                       !main_document_exec_at(
                           &libraries.term_universe, atom_ids,
                           n, block_end, NULL, NULL)) {
                    block_end++;
                }
                MainPettaBlockLoadResult loaded =
                    main_petta_load_declaration_block(
                        libraries.petta_program, &space, &registry,
                        &libraries.term_universe, atom_ids + pi,
                        block_end - pi,
                        profile &&
                            profile->id ==
                                CETTA_PROFILE_PETTA_TYPECHECK_V2,
                        petta_typecheck_policy);
                if (loaded != MAIN_PETTA_BLOCK_LOAD_OK) {
                    if (loaded == MAIN_PETTA_BLOCK_LOAD_FAILED) {
                        fprintf(
                            stderr,
                            "error: could not compile PeTTa declaration block\n");
                    }
                    rc = loaded == MAIN_PETTA_BLOCK_LOAD_TYPE_REJECTED
                        ? 2 : 1;
                    goto cleanup;
                }
                pi = block_end;
            }
        } else {
            int pi = 0;
            while (pi < n) {
                int exec_width = 0;
                if (main_document_exec_at(
                        &libraries.term_universe, atom_ids,
                        n, pi, NULL, &exec_width)) {
                    pi += exec_width;
                    continue;
                }
                int block_end = pi + 1;
                while (block_end < n &&
                       !main_document_exec_at(
                           &libraries.term_universe, atom_ids,
                           n, block_end, NULL, NULL)) {
                    block_end++;
                }
                if (!space_add_atom_ids_batch(
                        &space, atom_ids + pi,
                        (CettaCount)(block_end - pi))) {
                    fprintf(stderr,
                            "error: could not load declaration block\n");
                    rc = 1;
                    goto cleanup;
                }
                pi = block_end;
            }
        }
        goto petta_document_complete;
    }

    /* Process top-level atoms */
    i = 0;
    while (i < n) {
        if (lang_is_mm2) {
            Atom *at = atoms[i];

            /* ! prefix → evaluate and print */
            if (atom_is_symbol_id(at, g_builtin_syms.bang) && i + 1 < n) {
                Atom *expr = atoms[i + 1];
                ResultSet rs;
                result_set_init(&rs);
                eval_top_with_registry(&space, &eval_arena, &arena, &registry, expr, &rs);
                if (cetta_eval_session_process_exit_requested(
                        &libraries.session)) {
                    rc = cetta_eval_session_process_exit_code(
                        &libraries.session);
                    result_set_free(&rs);
                    goto cleanup;
                }
                write_results(output_spool, &rs, lang->id, profile);
                if (fflush(output_spool) != 0) {
                    fprintf(stderr, "error: could not write output spool\n");
                    result_set_free(&rs);
                    rc = 1;
                    goto cleanup;
                }
                bool stop_after_error = result_set_has_error(&rs);
                result_set_free(&rs);
                eval_release_temporary_spaces();
                eval_reset_form_gc_survivor();
                /* Reset ephemeral arena — frees all intermediate eval atoms.
                   This makes CeTTa safe for unlimited chaining iterations. */
                arena_free(&eval_arena);
                arena_init(&eval_arena);
                arena_set_runtime_kind(&eval_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
                arena_set_hashcons(
                    &eval_arena, eval_hashcons ? &hashcons_table : NULL);
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

        /* ! prefix and (syn:exec form) are the same source directive. */
        AtomId payload_id = CETTA_ATOM_ID_NONE;
        int exec_width = 0;
        if (main_document_exec_at(
                &libraries.term_universe, atom_ids, n, i,
                &payload_id, &exec_width)) {
            if (lang->id == CETTA_LANGUAGE_PETTA && profile &&
                profile->id == CETTA_PROFILE_PETTA_TYPECHECK_V2) {
                MainPettaBlockLoadResult checked = main_petta_check_forms(
                    libraries.petta_program, &space, &registry,
                    &libraries.term_universe, atom_ids + i, exec_width,
                    petta_typecheck_policy);
                if (checked != MAIN_PETTA_BLOCK_LOAD_OK) {
                    rc = checked == MAIN_PETTA_BLOCK_LOAD_TYPE_REJECTED
                        ? 2 : 1;
                    goto cleanup;
                }
            }
            if (lang->id == CETTA_LANGUAGE_PETTA) {
                cetta_petta_erase_typecheck_marks_document(
                    &libraries.term_universe, atom_ids + i,
                    exec_width);
                if (!main_document_exec_at(
                        &libraries.term_universe, atom_ids, n, i,
                        &payload_id, &exec_width)) {
                    fprintf(
                        stderr,
                        "error: could not normalize PeTTa source directive\n");
                    rc = 1;
                    goto cleanup;
                }
            }
            const PettaPlanNode *source_plan = NULL;
            if (lang->id == CETTA_LANGUAGE_PETTA) {
                Atom *source = term_universe_get_atom(
                    &libraries.term_universe, payload_id);
                source_plan = source
                    ? petta_program_plan_current(
                          libraries.petta_program, source)
                    : NULL;
                if (!source_plan) {
                    fprintf(
                        stderr,
                        "error: could not compile PeTTa occurrence plan\n");
                    rc = 1;
                    goto cleanup;
                }
            }
            Atom *expr = term_universe_copy_atom(&libraries.term_universe, &arena,
                                                 payload_id);
            ResultSet rs;
            EvalOutcome detailed;
            ResultSet *results = &rs;
            PrimeNeedTracePrinter trace = {0};
            if (!expr) {
                fprintf(stderr, "error: could not decode top-level eval form\n");
                rc = 1;
                goto cleanup;
            }
            if (emit_prime_need_trace) {
                eval_outcome_init(&detailed);
                results = &detailed.results;
                trace.out = stderr;
                trace.form = ++g_prime_need_trace_form;
                eval_top_with_registry_outcome(
                    &space, &eval_arena, &arena, &registry, expr,
                    &detailed, prime_need_trace_answer, &trace);
                fprintf(stderr,
                        "(prime-need:completion %" PRIu64 " %s"
                        " (occurrences %zu) (values %zu) (faults %zu)"
                        " (steps %" PRIu64 "))\n",
                        trace.form,
                        eval_completion_reason(detailed.completion),
                        (size_t)detailed.results.len,
                        (size_t)eval_outcome_value_count(&detailed),
                        (size_t)eval_outcome_fault_count(&detailed),
                        detailed.steps_spent);
            } else {
                result_set_init(&rs);
                if (lang->id == CETTA_LANGUAGE_PETTA) {
                    eval_top_with_registry_petta_plan(
                        &space, &eval_arena, &arena, &registry,
                        expr, source_plan, &rs);
                } else {
                    eval_top_with_registry(
                        &space, &eval_arena, &arena, &registry,
                        expr, &rs);
                }
            }
            if (cetta_eval_session_process_exit_requested(
                    &libraries.session)) {
                rc = cetta_eval_session_process_exit_code(
                    &libraries.session);
                if (emit_prime_need_trace)
                    eval_outcome_free(&detailed);
                else
                    result_set_free(&rs);
                prime_need_trace_printer_free(&trace);
                goto cleanup;
            }
            int typecheck_exit_code = 0;
            const char *typecheck_diagnostic = NULL;
            if (lang->id == CETTA_LANGUAGE_PETTA &&
                result_set_petta_typecheck_error(
                    results, &typecheck_exit_code,
                    &typecheck_diagnostic)) {
                fprintf(
                    stderr, "%s: %s\n",
                    typecheck_exit_code == 2
                        ? "PeTTa type error"
                        : "PeTTa typechecker fault",
                    typecheck_diagnostic
                        ? typecheck_diagnostic
                        : "runtime analysis judgment failed");
                rc = typecheck_exit_code;
                if (emit_prime_need_trace)
                    eval_outcome_free(&detailed);
                else
                    result_set_free(&rs);
                prime_need_trace_printer_free(&trace);
                goto cleanup;
            }
            write_results(output_spool, results, lang->id, profile);
            if (fflush(output_spool) != 0) {
                fprintf(stderr, "error: could not write output spool\n");
                if (emit_prime_need_trace)
                    eval_outcome_free(&detailed);
                else
                    result_set_free(&rs);
                prime_need_trace_printer_free(&trace);
                rc = 1;
                goto cleanup;
            }
            bool stop_after_error = result_set_has_error(results);
            if (trace.allocation_failed) {
                fprintf(stderr,
                        "error: could not allocate Prime receipt trace identity\n");
                stop_after_error = true;
                prime_need_trace_failed = true;
            }
            if (emit_prime_need_trace)
                eval_outcome_free(&detailed);
            else
                result_set_free(&rs);
            prime_need_trace_printer_free(&trace);
            eval_release_temporary_spaces();
            eval_reset_form_gc_survivor();
            arena_free(&eval_arena);
            arena_init(&eval_arena);
            arena_set_runtime_kind(&eval_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
            arena_set_hashcons(
                &eval_arena, eval_hashcons ? &hashcons_table : NULL);
            if (stop_after_error) {
                stop_document_sequence = true;
                break;
            }
            i += exec_width;
            continue;
        }

        if (lang->id == CETTA_LANGUAGE_PETTA) {
            int block_end = i + 1;
            while (block_end < n &&
                   !main_document_exec_at(
                       &libraries.term_universe, atom_ids,
                       n, block_end, NULL, NULL)) {
                block_end++;
            }
            MainPettaBlockLoadResult loaded =
                main_petta_load_declaration_block(
                    libraries.petta_program, &space, &registry,
                    &libraries.term_universe, atom_ids + i,
                    block_end - i,
                    profile &&
                        profile->id ==
                            CETTA_PROFILE_PETTA_TYPECHECK_V2,
                    petta_typecheck_policy);
            if (loaded != MAIN_PETTA_BLOCK_LOAD_OK) {
                if (loaded == MAIN_PETTA_BLOCK_LOAD_FAILED) {
                    fprintf(
                        stderr,
                        "error: could not compile PeTTa declaration block\n");
                }
                rc = loaded == MAIN_PETTA_BLOCK_LOAD_TYPE_REJECTED
                    ? 2 : 1;
                goto cleanup;
            }
            i = block_end;
            continue;
        }

        int block_end = i + 1;
        while (block_end < n &&
               !main_document_exec_at(
                   &libraries.term_universe, atom_ids,
                   n, block_end, NULL, NULL)) {
            block_end++;
        }
        bool prime_relational_plan =
            libraries.prime_relational_plan_enabled;
        bool loaded = prime_relational_plan
            ? main_load_planned_declaration_block(
                  libraries.petta_program, &space,
                  &libraries.term_universe, atom_ids + i,
                  block_end - i)
            : space_add_atom_ids_batch(
                  &space, atom_ids + i,
                  (CettaCount)(block_end - i));
        if (!loaded) {
            fprintf(stderr, "error: could not load declaration block\n");
            rc = 1;
            goto cleanup;
        }
        i = block_end;
    }

petta_document_complete:
    if (!stop_document_sequence &&
        lang->id == CETTA_LANGUAGE_PETTA &&
        petta_file_arg_cursor >= 0 &&
        petta_file_arg_cursor + 1 < petta_file_arg_end) {
        petta_file_arg_cursor++;
        filename = argv[petta_file_arg_cursor];
        script_path = filename;
        cetta_library_context_set_script_path(&libraries, script_path);

        free(atom_ids);
        atom_ids = NULL;
        cleanup.atom_ids = NULL;
        document_reader_error[0] = '\0';
        n = parse_metta_file_ids_diagnostic(
            filename, &libraries.term_universe, &atom_ids,
            document_reader_error, sizeof(document_reader_error));
        cleanup.atom_ids = atom_ids;
        if (n < 0) {
            if (document_reader_error[0]) {
                fprintf(stderr, "error: compiled PeTTa reader: %s\n",
                        document_reader_error);
            }
            fprintf(stderr, "error: could not read %s\n", filename);
            rc = 1;
            goto cleanup;
        }
        goto process_petta_document;
    }

    if (compile_mode) {
        compile_space_to_llvm(&space, &arena, stdout);
        rc = 0;
        goto cleanup;
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

    rc = prime_need_trace_failed ? 1 : 0;

cleanup:
    cetta_main_cleanup(&cleanup);
    return rc;
}
