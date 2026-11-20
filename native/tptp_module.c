#define _XOPEN_SOURCE 700

#include "native/tptp_module.h"
#include "native/langdef_module.h"
#include "library.h"
#include "native_handle.h"
#include "symbol.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TPTP_READER_HANDLE_KIND "cetta.tptp-reader.v1"

/* A reader handle's resource: the prepared reader of one profile's snapshot
 * and the prepared equations beside it. */
typedef struct {
    CettaTptpPreparedReaderV1 reader;
    /* The compact morphism the build checked for this profile, or NULL with
     * the reason in compact_error. */
    CettaDeterministicEquationPlanV1 *compact;
    char compact_error[256];
    /* Its inverse, for printing compact values, and the wrappers of the
     * grammar: the constructors the morphism maps to their child. */
    CettaDeterministicEquationPlanV1 *print;
    char print_error[256];
    SymbolId *wrappers;
    uint32_t wrapper_len;
    /* Owned by the process's shared readers rather than by one handle. */
    bool shared;
} CettaTptpReaderV1;

static Atom *tptp_error(Arena *arena, Atom *source, const char *message) {
    return atom_error(arena, source, atom_string(arena, message));
}

/* (NAME ARGUMENT...) */
static Atom *tptp_expr(Arena *arena, const char *name, Atom **arguments,
                       uint32_t argument_len) {
    Atom **elements = arena_alloc(arena, sizeof(*elements) * (argument_len + 1u));
    elements[0] = atom_symbol(arena, name);
    for (uint32_t index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    return atom_expr(arena, elements, argument_len + 1u);
}

/* The module answers as LangDef's natives do (cetta_langdef_return), so the
 * library's wrappers take every answer alike. */
static Atom *tptp_answer(Arena *arena, const char *name, Atom **arguments,
                         uint32_t argument_len) {
    return cetta_langdef_return(arena,
                                tptp_expr(arena, name, arguments, argument_len));
}

static int64_t tptp_count(uint64_t value) {
    return value > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)value;
}

static const char *tptp_kernel_name(const PPTableSnapshotV1 *snapshot) {
    return snapshot->kernel == PP_TABLE_SNAPSHOT_V1_KERNEL_SLR ? "slr" : "glr";
}

/* ------------------------------------------------------------ readers -- */

static void tptp_reader_free(void *opaque) {
    CettaTptpReaderV1 *resource = opaque;

    if (!resource)
        return;
    cetta_deterministic_equation_plan_v1_free(resource->compact);
    cetta_deterministic_equation_plan_v1_free(resource->print);
    free(resource->wrappers);
    cetta_tptp_prepared_reader_free_v1(&resource->reader);
    free(resource);
}

/* A prepared presentation set beside a snapshot: compact-v1/NAME lists the
 * checked presentations, which load as LangDef equations do
 * (cetta_langdef_deterministic_equations_load_manifest_v1). */
CettaDeterministicEquationPlanV1 *cetta_tptp_compact_manifest_plan_v1(
    const char *snapshot_path, const char *manifest_name,
    char *reason, size_t reason_size) {
    char manifest_path[PATH_MAX];
    const char *slash = strrchr(snapshot_path, '/');
    int written;

    reason[0] = '\0';
    if (!slash) {
        snprintf(reason, reason_size, "snapshot path has no directory");
        return NULL;
    }
    written = snprintf(manifest_path, sizeof(manifest_path), "%.*s/compact-v1/%s",
                       (int)(slash - snapshot_path), snapshot_path, manifest_name);
    if (written < 0 || (size_t)written >= sizeof(manifest_path)) {
        snprintf(reason, reason_size, "snapshot path is too long");
        return NULL;
    }
    return cetta_langdef_deterministic_equations_load_manifest_v1(
        manifest_path, reason, reason_size);
}

/* The reader of the snapshot at snapshot_path, bound to the official
 * SyntaxBNF and to profile, with the compact morphism and its printer when
 * the build prepared them. */
static CettaTptpReaderV1 *tptp_reader_load(const char *snapshot_path,
                                           const char *profile,
                                           char *error, size_t error_size) {
    CettaTptpReaderV1 *resource = calloc(1u, sizeof(*resource));

    if (!resource) {
        if (error && error_size)
            snprintf(error, error_size, "tptp reader allocation failed");
        return NULL;
    }
    cetta_tptp_prepared_reader_init_v1(&resource->reader);
    if (!cetta_tptp_prepared_reader_load_bound_v1(
            &resource->reader, snapshot_path,
            CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1, profile, NULL,
            error, error_size)) {
        tptp_reader_free(resource);
        return NULL;
    }
    resource->compact = cetta_tptp_compact_manifest_plan_v1(
        snapshot_path, "manifest", resource->compact_error,
        sizeof(resource->compact_error));
    if (resource->compact) {
        (void)cetta_tptp_compact_wrappers_v1(
            resource->compact, &resource->wrappers, &resource->wrapper_len);
        resource->print = cetta_tptp_compact_manifest_plan_v1(
            snapshot_path, "print-manifest", resource->print_error,
            sizeof(resource->print_error));
    } else {
        snprintf(resource->print_error, sizeof(resource->print_error), "%s",
                 resource->compact_error);
    }
    return resource;
}

/* A loaded reader is immutable, so the process loads each snapshot file once
 * for each profile and every handle to it shares that reader.  A snapshot
 * rewritten on disk is another file and loads again; readers already
 * loaded stay valid for the handles that hold them. */
typedef struct TptpSharedReaderV1 {
    struct TptpSharedReaderV1 *next;
    char *path;
    char *profile;
    dev_t device;
    ino_t inode;
    off_t size;
    struct timespec modified;
    CettaTptpReaderV1 *reader;
} TptpSharedReaderV1;

static pthread_mutex_t tptp_shared_readers_lock = PTHREAD_MUTEX_INITIALIZER;
static TptpSharedReaderV1 *tptp_shared_readers;

static bool tptp_shared_reader_is(const TptpSharedReaderV1 *entry,
                                  const char *path, const char *profile,
                                  const struct stat *st) {
    return strcmp(entry->path, path) == 0 &&
           strcmp(entry->profile, profile) == 0 &&
           entry->device == st->st_dev && entry->inode == st->st_ino &&
           entry->size == st->st_size &&
           entry->modified.tv_sec == st->st_mtim.tv_sec &&
           entry->modified.tv_nsec == st->st_mtim.tv_nsec;
}

static CettaTptpReaderV1 *tptp_reader_shared(const char *snapshot_path,
                                             const char *profile,
                                             char *error, size_t error_size) {
    struct stat st;
    TptpSharedReaderV1 *entry;
    CettaTptpReaderV1 *reader = NULL;

    /* A file that cannot be examined is reported by the load itself. */
    if (stat(snapshot_path, &st) != 0)
        return tptp_reader_load(snapshot_path, profile, error, error_size);
    pthread_mutex_lock(&tptp_shared_readers_lock);
    for (entry = tptp_shared_readers; entry; entry = entry->next) {
        if (tptp_shared_reader_is(entry, snapshot_path, profile, &st)) {
            reader = entry->reader;
            goto done;
        }
    }
    entry = calloc(1u, sizeof(*entry));
    if (!entry || !(entry->path = strdup(snapshot_path)) ||
        !(entry->profile = strdup(profile))) {
        if (entry) {
            free(entry->path);
            free(entry);
        }
        if (error && error_size)
            snprintf(error, error_size, "tptp reader allocation failed");
        goto done;
    }
    reader = tptp_reader_load(snapshot_path, profile, error, error_size);
    if (!reader) {
        free(entry->profile);
        free(entry->path);
        free(entry);
        goto done;
    }
    reader->shared = true;
    entry->device = st.st_dev;
    entry->inode = st.st_ino;
    entry->size = st.st_size;
    entry->modified = st.st_mtim;
    entry->reader = reader;
    entry->next = tptp_shared_readers;
    tptp_shared_readers = entry;
done:
    pthread_mutex_unlock(&tptp_shared_readers_lock);
    return reader;
}

/* A handle's release: a shared reader stays with the process. */
static void tptp_reader_release(void *opaque) {
    CettaTptpReaderV1 *resource = opaque;

    if (resource && !resource->shared)
        tptp_reader_free(resource);
}

/* The live reader a call names as its first argument, when the call has
 * its arity. */
static CettaTptpReaderV1 *tptp_reader_arg(CettaLibraryContext *ctx,
                                          Atom **args, uint32_t nargs,
                                          uint32_t arity) {
    uint64_t id;

    if (nargs != arity ||
        !cetta_native_handle_arg(args[0], TPTP_READER_HANDLE_KIND, &id))
        return NULL;
    return cetta_native_handle_get(ctx, TPTP_READER_HANDLE_KIND, id);
}

/* The refusal of a call that names no live reader: what it gave, or
 * NoReader. */
static Atom *tptp_reader_mismatch(Arena *arena, Atom *given) {
    Atom *mismatch[1] = {given ? given : atom_symbol(arena, "NoReader")};
    return tptp_answer(arena, "TPTP:ReaderMismatch", mismatch, 1u);
}

/* --------------------------------------------------- compact stages -- */

/* The number of atoms in a term, counted without recursion. */
static uint64_t tptp_term_size(const Atom *term) {
    const Atom **stack;
    size_t len = 0u;
    size_t cap = 256u;
    uint64_t size = 0u;

    if (!term || !(stack = malloc(cap * sizeof(*stack))))
        return 0u;
    stack[len++] = term;
    while (len > 0u) {
        const Atom *atom = stack[--len];
        size++;
        if (atom->kind != ATOM_EXPR)
            continue;
        for (CettaExprLen i = 0u; i < atom->expr.len; i++) {
            if (len == cap) {
                const Atom **grown = realloc(stack, 2u * cap * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    return size;
                }
                stack = grown;
                cap *= 2u;
            }
            stack[len++] = atom->expr.elems[i];
        }
    }
    free(stack);
    return size;
}

/* The compact stage of a TPTP read: the records of a canonical file term
 * under a leaf policy, by the checked compact morphism.  The morphism is
 * structural, so its work and its continuation depth are bounded by the size
 * of the canonical term: an expression or a list of n elements is walked as
 * a spine n deep.  tptp:read and the corpus tools both run this one
 * function. */
bool cetta_tptp_compact_records_v1(
    const CettaDeterministicEquationPlanV1 *compact, const char *policy,
    Atom *canonical, Arena *arena, Atom **records_out,
    uint64_t *work_used_out, CettaDeterministicEquationStatusV1 *status_out,
    char *error, size_t error_size) {
    uint64_t size;
    uint64_t work_used = 0u;
    CettaDeterministicEquationStatusV1 status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    Atom *call_parts[3];
    Atom *call = NULL;
    bool ok = false;
    if (records_out)
        *records_out = NULL;
    if (compact && policy && canonical && arena && records_out) {
        size = tptp_term_size(canonical);
        call_parts[0] = atom_symbol(arena, "tptp-compact:file");
        call_parts[1] = atom_symbol(arena, policy);
        call_parts[2] = canonical;
        call = atom_expr(arena, call_parts, 3u);
        ok = call && cetta_deterministic_equation_plan_v1_apply_counted(
            compact, call, cetta_langdef_deterministic_vocabulary_v1.primitive,
            cetta_langdef_deterministic_vocabulary_v1.context, arena,
            (uint32_t)(size > (UINT32_MAX - 65536u) / 16u
                           ? UINT32_MAX : 16u * size + 65536u),
            size > (UINT64_MAX - 1000000u) / 1024u
                ? UINT64_MAX : 1024u * size + 1000000u,
            &work_used, records_out, &status, error, error_size);
    }
    if (work_used_out)
        *work_used_out = work_used;
    if (status_out)
        *status_out = status;
    return ok;
}

/* The constructors whose productions only wrap their child's value:
 * (tptp-compact:v P (bnf C x)) = (tptp-compact:v P x). */
bool cetta_tptp_compact_wrappers_v1(
    const CettaDeterministicEquationPlanV1 *compact,
    SymbolId **wrappers_out, uint32_t *len_out) {
    uint32_t count;
    SymbolId v;
    SymbolId bnf;
    SymbolId *wrappers;
    uint32_t len = 0u;
    if (!compact || !wrappers_out || !len_out)
        return false;
    *wrappers_out = NULL;
    *len_out = 0u;
    count = cetta_deterministic_equation_plan_v1_rule_count(compact);
    v = symbol_intern_cstr(g_symbols, "tptp-compact:v");
    bnf = symbol_intern_cstr(g_symbols, "bnf");
    wrappers = calloc(count ? count : 1u, sizeof(*wrappers));
    if (!wrappers)
        return false;
    for (uint32_t r = 0u; r < count; r++) {
        const Atom *left = NULL;
        const Atom *right = NULL;
        const Atom *node;
        if (!cetta_deterministic_equation_plan_v1_rule_view(compact, r, NULL,
                                                           &left, &right) ||
            !left || !right || left->kind != ATOM_EXPR || left->expr.len != 3u ||
            right->kind != ATOM_EXPR || right->expr.len != 3u ||
            left->expr.elems[0]->kind != ATOM_SYMBOL || left->expr.elems[0]->sym_id != v ||
            right->expr.elems[0]->kind != ATOM_SYMBOL || right->expr.elems[0]->sym_id != v ||
            left->expr.elems[1]->kind != ATOM_VAR || right->expr.elems[1]->kind != ATOM_VAR ||
            !var_id_eq(left->expr.elems[1]->var_id, right->expr.elems[1]->var_id))
            continue;
        node = left->expr.elems[2];
        if (node->kind != ATOM_EXPR || node->expr.len != 3u ||
            node->expr.elems[0]->kind != ATOM_SYMBOL || node->expr.elems[0]->sym_id != bnf ||
            node->expr.elems[1]->kind != ATOM_SYMBOL || node->expr.elems[2]->kind != ATOM_VAR ||
            right->expr.elems[2]->kind != ATOM_VAR ||
            !var_id_eq(node->expr.elems[2]->var_id, right->expr.elems[2]->var_id))
            continue;
        wrappers[len++] = node->expr.elems[1]->sym_id;
    }
    *wrappers_out = wrappers;
    *len_out = len;
    return true;
}

/* The print stage: compact records back to TPTP text, their canonical
 * terms by the prepared inverse, each printed by the grammar with the
 * wrappers its position needs, one input per line.  tptp:print and the
 * corpus tools both run this one function.  On failure *failure_out names
 * the step: the inverse (with *status_out), a result that is not a list of
 * inputs, or the printing of input *index_out. */
bool cetta_tptp_compact_print_v1(
    const CettaTptpPreparedReaderV1 *reader,
    const CettaGrammarCanonicalTableV1 *table,
    const CettaDeterministicEquationPlanV1 *print,
    const SymbolId *wrappers, uint32_t wrapper_len,
    const Atom *records, Arena *arena,
    char **text_out, size_t *len_out,
    CettaTptpCompactPrintFailureV1 *failure_out, uint64_t *index_out,
    Atom **culprit_out, CettaDeterministicEquationStatusV1 *status_out,
    uint64_t *work_used_out, char *error, size_t error_size) {
    Atom *call_parts[2];
    Atom *call;
    Atom *canonical = NULL;
    CettaDeterministicEquationStatusV1 status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    uint64_t size;
    uint64_t work_used = 0u;
    CettaGrammarCanonicalTableV1 *built = NULL;
    char *text = NULL;
    size_t text_len = 0u;
    size_t text_cap = 0u;
    Atom *const *inputs = NULL;
    CettaExprLen input_count = 0u;
    if (text_out)
        *text_out = NULL;
    if (len_out)
        *len_out = 0u;
    if (failure_out)
        *failure_out = CETTA_TPTP_COMPACT_PRINT_INVERSE_V1;
    if (culprit_out)
        *culprit_out = NULL;
    if (!reader || !print || !records || !arena || !text_out || !len_out ||
        records->kind != ATOM_EXPR)
        return false;
    size = tptp_term_size(records);
    call_parts[0] = atom_symbol(arena, "tptp-canonical:file");
    call_parts[1] = (Atom *)records;
    call = atom_expr(arena, call_parts, 2u);
    if (!call ||
        !cetta_deterministic_equation_plan_v1_apply_counted(
            print, call, cetta_langdef_deterministic_vocabulary_v1.primitive,
            cetta_langdef_deterministic_vocabulary_v1.context, arena,
            (uint32_t)(size > (UINT32_MAX - 65536u) / 16u
                           ? UINT32_MAX : 16u * size + 65536u),
            size > (UINT64_MAX - 1000000u) / 4096u
                ? UINT64_MAX : 4096u * size + 1000000u,
            &work_used, &canonical, &status, error, error_size) ||
        !canonical || canonical->kind != ATOM_EXPR) {
        if (status_out)
            *status_out = status;
        if (work_used_out)
            *work_used_out = work_used;
        return false;
    }
    if (status_out)
        *status_out = status;
    if (work_used_out)
        *work_used_out = work_used;
    if (!atom_sequence_view(canonical, &inputs, &input_count)) {
        if (failure_out)
            *failure_out = CETTA_TPTP_COMPACT_PRINT_NOT_RECORDS_V1;
        if (culprit_out)
            *culprit_out = canonical;
        return false;
    }
    if (!table)
        table = reader->canonical;
    if (!table) {
        if (!cetta_tptp_snapshot_canonical_table_v1(&reader->snapshot, &built,
                                                    error, error_size)) {
            if (failure_out)
                *failure_out = CETTA_TPTP_COMPACT_PRINT_MEMORY_V1;
            return false;
        }
        table = built;
    }
    for (CettaExprLen index = 0u; index < input_count; index++) {
        char *input = NULL;
        size_t input_len = 0u;
        if (!cetta_tptp_snapshot_canonical_print_wrapped_v1(
                &reader->snapshot, table, inputs[index],
                "TPTP_input", wrappers, wrapper_len,
                &input, &input_len, error, error_size)) {
            if (failure_out)
                *failure_out = CETTA_TPTP_COMPACT_PRINT_INPUT_V1;
            if (index_out)
                *index_out = (uint64_t)index;
            if (culprit_out)
                *culprit_out = inputs[index];
            cetta_grammar_canonical_table_free_v1(built);
            free(text);
            return false;
        }
        if (text_len + input_len + 2u > text_cap) {
            size_t next = text_cap ? text_cap : 256u;
            char *grown;
            while (next < text_len + input_len + 2u)
                next *= 2u;
            grown = realloc(text, next);
            if (!grown) {
                free(input);
                free(text);
                cetta_grammar_canonical_table_free_v1(built);
                if (failure_out)
                    *failure_out = CETTA_TPTP_COMPACT_PRINT_MEMORY_V1;
                return false;
            }
            text = grown;
            text_cap = next;
        }
        memcpy(text + text_len, input, input_len);
        text_len += input_len;
        text[text_len++] = '\n';
        text[text_len] = '\0';
        free(input);
    }
    cetta_grammar_canonical_table_free_v1(built);
    *text_out = text;
    *len_out = text_len;
    return true;
}

/* -------------------------------------------------------- operations -- */

/* The refusal of a read: where its input stopped lexing or parsing, an
 * ambiguity, or the work limit it reached; NULL for any other failure. */
static Atom *tptp_read_outcome(Arena *arena,
                               const CettaTptpReadOutcomeV1 *outcome) {
    Atom *arguments[3];

    switch (outcome->status) {
    case CETTA_TPTP_READ_LEX_REJECT_V1:
        arguments[0] = atom_int(arena, (int64_t)outcome->byte_offset);
        return tptp_expr(arena, "TPTP:LexReject", arguments, 1u);
    case CETTA_TPTP_READ_NO_PARSE_V1:
        arguments[0] = atom_int(arena, (int64_t)outcome->byte_offset);
        arguments[1] = atom_symbol(arena, "TPTP_file");
        return tptp_expr(arena, "TPTP:NoParse", arguments, 2u);
    case CETTA_TPTP_READ_AMBIGUOUS_V1:
        arguments[0] = atom_symbol(arena, "TPTP_file");
        return tptp_expr(arena, "TPTP:Ambiguous", arguments, 1u);
    case CETTA_TPTP_READ_RESOURCE_LIMIT_V1:
        arguments[0] = atom_int(arena, tptp_count(outcome->work));
        arguments[1] = atom_int(arena, tptp_count(outcome->limit));
        arguments[2] = atom_symbol(arena, "TPTP_file");
        return tptp_expr(arena, "TPTP:ResourceLimit", arguments, 3u);
    default:
        return NULL;
    }
}

/* A read's answer: the term it read, or its refusal (tptp_read_outcome), or
 * an error naming what failed. */
static Atom *tptp_read_answer(Arena *arena, Atom *head, bool read,
                              Atom *result,
                              const CettaTptpReadOutcomeV1 *outcome,
                              const char *error, const char *failure) {
    Atom *refusal;

    if (read)
        return cetta_langdef_return(arena, result);
    refusal = tptp_read_outcome(arena, outcome);
    if (refusal)
        return cetta_langdef_return(arena, refusal);
    return tptp_error(arena, head, error[0] ? error : failure);
}

static Atom *tptp_syntaxbnf_digest(CettaLibraryContext *ctx, Arena *arena,
                                   Atom *head, Atom **args, uint32_t nargs) {
    const char *path = NULL;
    char digest[65];
    char error[512] = {0};

    if (nargs != 1u || !cetta_langdef_text_arg(args[0], &path))
        return tptp_error(arena, head, "tptp digest expects a SyntaxBNF path");
    if (!cetta_tptp_file_sha256_hex_v1(path, digest, error, sizeof(error)))
        return tptp_error(arena, head,
                          error[0] ? error : "tptp SyntaxBNF digest failed");
    return cetta_langdef_return(arena, atom_string(arena, digest));
}

static Atom *tptp_write_atom(CettaLibraryContext *ctx, Arena *arena,
                             Atom *head, Atom **args, uint32_t nargs) {
    const char *path = NULL;
    char error[512] = {0};

    if (nargs != 2u || !cetta_langdef_text_arg(args[0], &path))
        return tptp_error(arena, head, "tptp write-atom expects a path and an atom");
    if (!cetta_tptp_write_atom_v1(path, args[1], arena, error, sizeof(error)))
        return tptp_error(arena, head, error[0] ? error : "tptp write-atom failed");
    return cetta_langdef_return(arena, atom_symbol(arena, "True"));
}

static Atom *tptp_read_atom(CettaLibraryContext *ctx, Arena *arena,
                            Atom *head, Atom **args, uint32_t nargs) {
    const char *path = NULL;
    Atom *result = NULL;
    char error[512] = {0};

    if (nargs != 1u || !cetta_langdef_text_arg(args[0], &path))
        return tptp_error(arena, head, "tptp read-atom expects a path");
    if (!cetta_tptp_read_atom_v1(path, arena, &result, error, sizeof(error)))
        return tptp_error(arena, head, error[0] ? error : "tptp read-atom failed");
    return cetta_langdef_return(arena, result);
}

/* (NAME SYNTAX-DIGEST ARTIFACT-DIGEST PROFILE KERNEL CONFLICTS BYTES) of the
 * snapshot loaded from path. */
static Atom *tptp_snapshot_description(Arena *arena, const char *name,
                                       const PPTableSnapshotV1 *loaded,
                                       const char *path) {
    struct stat st;
    Atom *parts[6];

    memset(&st, 0, sizeof(st));
    (void)stat(path, &st);
    parts[0] = atom_string(arena, loaded->syntax_digest);
    parts[1] = atom_string(arena, loaded->artifact_digest);
    parts[2] = atom_symbol(arena, loaded->profile);
    parts[3] = atom_symbol(arena, tptp_kernel_name(loaded));
    parts[4] = atom_int(arena, (int64_t)loaded->conflict_len);
    parts[5] = atom_int(arena, (int64_t)st.st_size);
    return tptp_answer(arena, name, parts, 6u);
}

/* (pack out), or (pack baseline out) for a corpus-compatible pack
 * constructed against its strict snapshot. */
static Atom *tptp_snapshot_construct(CettaLibraryContext *ctx, Arena *arena,
                                     Atom *head, Atom **args, uint32_t nargs) {
    const char *pack_path = NULL;
    const char *baseline_path = NULL;
    const char *out_path = NULL;
    PPTableSnapshotV1 loaded;
    Atom *description;
    char error[512] = {0};

    if ((nargs != 2u && nargs != 3u) ||
        !cetta_langdef_text_arg(args[0], &pack_path) ||
        (nargs == 3u && !cetta_langdef_text_arg(args[1], &baseline_path)) ||
        !cetta_langdef_text_arg(args[nargs - 1u], &out_path))
        return tptp_error(arena, head,
                          "tptp snapshot construct expects pack, optional baseline "
                          "and out paths");
    if (!cetta_tptp_snapshot_construct_from_pack_v1(
            pack_path, baseline_path, out_path, error, sizeof(error)))
        return tptp_error(arena, head,
                          error[0] ? error : "tptp snapshot construct failed");
    pp_table_snapshot_v1_init(&loaded);
    if (!cetta_tptp_snapshot_load_v1(
            &loaded, out_path, CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
            error, sizeof(error))) {
        pp_table_snapshot_v1_free(&loaded);
        return tptp_error(arena, head,
                          error[0] ? error : "tptp snapshot reload failed");
    }
    description = tptp_snapshot_description(
        arena, "TptpSnapshotConstructionV1", &loaded, out_path);
    pp_table_snapshot_v1_free(&loaded);
    return description;
}

static Atom *tptp_snapshot_info(CettaLibraryContext *ctx, Arena *arena,
                                Atom *head, Atom **args, uint32_t nargs) {
    const char *path = NULL;
    PPTableSnapshotV1 loaded;
    Atom *description;
    char error[512] = {0};

    if (nargs != 1u || !cetta_langdef_text_arg(args[0], &path))
        return tptp_error(arena, head, "tptp snapshot info expects a path");
    pp_table_snapshot_v1_init(&loaded);
    if (!cetta_tptp_snapshot_load_v1(
            &loaded, path, CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1,
            error, sizeof(error))) {
        pp_table_snapshot_v1_free(&loaded);
        if (error[0] && strstr(error, "DigestMismatch")) {
            Atom *mismatch[1] = {atom_string(arena, path)};
            return tptp_answer(arena, "TPTP:DigestMismatch", mismatch, 1u);
        }
        return tptp_error(arena, head,
                          error[0] ? error : "tptp snapshot info failed");
    }
    description = tptp_snapshot_description(arena, "TptpSnapshotInfoV1",
                                            &loaded, path);
    pp_table_snapshot_v1_free(&loaded);
    return description;
}

/* A reader of the strict or the corpus-compatible profile, from the
 * snapshot installed under the runtime root or from a given one. */
static Atom *tptp_reader_open(CettaLibraryContext *ctx, Arena *arena,
                              Atom *head, Atom **args, uint32_t nargs) {
    CettaTptpReaderV1 *resource;
    const char *snapshot_path;
    const char *profile;
    char installed_path[PATH_MAX];
    char error[512] = {0};
    uint64_t id;
    Atom *handle;

    if (nargs != 1u && nargs != 2u)
        return tptp_error(arena, head,
                          "tptp reader open expects a profile and optional snapshot path");
    if (atom_is_symbol(args[0], "strict")) {
        snapshot_path = CETTA_TPTP_OFFICIAL_SNAPSHOT_PATH_V1;
        profile = "strict";
    } else if (atom_is_symbol(args[0], "corpus-compatible")) {
        snapshot_path = CETTA_TPTP_CORPUS_COMPATIBLE_SNAPSHOT_PATH_V1;
        profile = "corpus-compatible";
    } else {
        return tptp_answer(arena, "TPTP:UnknownReaderProfile", args, 1u);
    }
    if (nargs == 2u && !cetta_langdef_text_arg(args[1], &snapshot_path))
        return tptp_error(arena, head, "tptp reader snapshot expects a path");
    if (nargs == 1u) {
        int written;
        if (!ctx->root_dir[0])
            return tptp_error(arena, head, "runtime root is not established");
        written = snprintf(installed_path, sizeof(installed_path), "%s/%s",
                           ctx->root_dir, snapshot_path);
        if (written < 0 || (size_t)written >= sizeof(installed_path))
            return tptp_error(arena, head, "tptp reader snapshot path is too long");
        snapshot_path = installed_path;
    }
    resource = tptp_reader_shared(snapshot_path, profile, error, sizeof(error));
    if (!resource)
        return tptp_error(arena, head,
                          error[0] ? error : "tptp reader snapshot load failed");
    if (!cetta_native_handle_alloc(ctx, TPTP_READER_HANDLE_KIND, resource,
                                   tptp_reader_release, &id)) {
        tptp_reader_release(resource);
        return tptp_error(arena, head, "tptp reader handle allocation failed");
    }
    handle = cetta_native_handle_owned_atom(ctx, arena, TPTP_READER_HANDLE_KIND, id);
    if (!handle) {
        cetta_native_handle_close(ctx, TPTP_READER_HANDLE_KIND, id);
        handle = tptp_error(arena, head,
                            "native handle publication allocation failed");
    }
    return cetta_langdef_return(arena, handle);
}

static Atom *tptp_reader_info(CettaLibraryContext *ctx, Arena *arena,
                              Atom *head, Atom **args, uint32_t nargs) {
    CettaTptpReaderV1 *resource = tptp_reader_arg(ctx, args, nargs, 1u);
    const PPTableSnapshotV1 *snapshot;
    Atom *parts[4];

    if (!resource)
        return tptp_reader_mismatch(arena, nargs == 1u ? args[0] : NULL);
    snapshot = &resource->reader.snapshot;
    parts[0] = atom_string(arena, snapshot->syntax_digest);
    parts[1] = atom_symbol(arena, tptp_kernel_name(snapshot));
    parts[2] = atom_int(arena, (int64_t)snapshot->conflict_len);
    parts[3] = atom_int(arena, (int64_t)snapshot->slr.production_len);
    return tptp_answer(arena, "TptpReaderInfoV1", parts, 4u);
}

static Atom *tptp_reader_artifact_info(CettaLibraryContext *ctx, Arena *arena,
                                       Atom *head, Atom **args, uint32_t nargs) {
    CettaTptpReaderV1 *resource = tptp_reader_arg(ctx, args, nargs, 1u);
    const PPTableSnapshotV1 *snapshot;
    Atom *parts[6];

    if (!resource)
        return tptp_reader_mismatch(arena, nargs == 1u ? args[0] : NULL);
    snapshot = &resource->reader.snapshot;
    parts[0] = atom_string(arena, snapshot->syntax_digest);
    parts[1] = atom_string(arena, snapshot->artifact_digest);
    parts[2] = atom_symbol(arena, snapshot->profile);
    parts[3] = atom_symbol(arena, tptp_kernel_name(snapshot));
    parts[4] = atom_int(arena, (int64_t)snapshot->conflict_len);
    parts[5] = atom_int(arena, (int64_t)snapshot->slr.production_len);
    return tptp_answer(arena, "TptpReaderArtifactInfoV1", parts, 6u);
}

/* The text of a canonical term occupying a position of the named grammar
 * symbol. */
static Atom *tptp_reader_canonical_print(CettaLibraryContext *ctx,
                                         Arena *arena, Atom *head,
                                         Atom **args, uint32_t nargs) {
    CettaTptpReaderV1 *resource = tptp_reader_arg(ctx, args, nargs, 3u);
    const char *sort = NULL;
    char *text = NULL;
    size_t text_len = 0u;
    char error[512] = {0};
    Atom *result;

    if (!resource)
        return tptp_reader_mismatch(arena, nargs > 0u ? args[0] : NULL);
    if (!cetta_langdef_text_arg(args[1], &sort))
        return tptp_error(arena, head,
                          "tptp canonical print expects a grammar symbol");
    if (!cetta_tptp_snapshot_canonical_print_v1(
            &resource->reader.snapshot, resource->reader.canonical, args[2],
            sort, &text, &text_len, error, sizeof(error)))
        return tptp_error(arena, head,
                          error[0] ? error : "tptp canonical print failed");
    result = atom_string(arena, text);
    free(text);
    return cetta_langdef_return(arena, result);
}

/* The LanguageDef of the reader's canonical terms, derived from the
 * grammar. */
static Atom *tptp_reader_canonical_language(CettaLibraryContext *ctx,
                                            Arena *arena, Atom *head,
                                            Atom **args, uint32_t nargs) {
    CettaTptpReaderV1 *resource = tptp_reader_arg(ctx, args, nargs, 1u);
    Atom *language = NULL;
    char error[512] = {0};

    if (!resource)
        return tptp_reader_mismatch(arena, nargs == 1u ? args[0] : NULL);
    if (!cetta_tptp_snapshot_canonical_language_v1(
            &resource->reader.snapshot, arena, &language,
            error, sizeof(error)))
        return tptp_error(arena, head,
                          error[0] ? error : "tptp canonical language failed");
    return cetta_langdef_return(arena, language);
}

/* Compact records back to TPTP text: their canonical terms by the prepared
 * inverse, printed by the grammar with the wrappers each position needs,
 * one input per line. */
static Atom *tptp_reader_print_compact(CettaLibraryContext *ctx, Arena *arena,
                                       Atom *head, Atom **args, uint32_t nargs) {
    CettaTptpReaderV1 *resource = tptp_reader_arg(ctx, args, nargs, 2u);
    CettaDeterministicEquationStatusV1 status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    CettaTptpCompactPrintFailureV1 failure =
        CETTA_TPTP_COMPACT_PRINT_INVERSE_V1;
    uint64_t failing_index = 0u;
    Atom *culprit = NULL;
    char *text = NULL;
    size_t text_len = 0u;
    char error[512] = {0};
    const Atom *records;
    Atom *result;

    if (!resource)
        return tptp_reader_mismatch(arena, nargs > 0u ? args[0] : NULL);
    if (!resource->print) {
        Atom *reason[1] = {atom_string(arena, resource->print_error)};
        return tptp_answer(arena, "TPTP:CompactPrinterUnavailable", reason, 1u);
    }
    /* The records arrive quoted, so no lane evaluates them on the way; a
     * caller may quote them too. */
    records = args[1];
    while (records && records->kind == ATOM_EXPR && records->expr.len == 2u &&
           atom_is_symbol(records->expr.elems[0], "quote"))
        records = records->expr.elems[1];
    if (!records || records->kind != ATOM_EXPR)
        return tptp_answer(arena, "TPTP:NotCompactRecords", &args[1], 1u);
    if (!cetta_tptp_compact_print_v1(
            &resource->reader, NULL, resource->print,
            resource->wrappers, resource->wrapper_len, records, arena,
            &text, &text_len, &failure, &failing_index, &culprit,
            &status, NULL, error, sizeof(error))) {
        if (failure == CETTA_TPTP_COMPACT_PRINT_NOT_RECORDS_V1)
            return tptp_answer(arena, "TPTP:NotCompactRecords", &culprit, 1u);
        if (failure == CETTA_TPTP_COMPACT_PRINT_INPUT_V1) {
            Atom *parts[3] = {
                atom_int(arena, (int64_t)failing_index),
                atom_string(arena, error[0] ? error : "canonical printing failed"),
                culprit};
            return tptp_answer(arena, "TPTP:CompactPrintFailure", parts, 3u);
        }
        if (failure == CETTA_TPTP_COMPACT_PRINT_MEMORY_V1)
            return tptp_error(arena, head, "compact printing out of memory");
        {
            Atom *parts[2] = {
                atom_symbol(arena, cetta_deterministic_equation_status_name_v1(status)),
                atom_string(arena, error[0] ? error : "compact printing failed")};
            return tptp_answer(arena, "TPTP:CompactPrintFailure", parts, 2u);
        }
    }
    result = atom_string(arena, text ? text : "");
    free(text);
    return cetta_langdef_return(arena, result);
}

typedef enum {
    TPTP_READ_TEXT,
    TPTP_READ_FILE,
    TPTP_READ_DOCUMENT
} TptpReadSource;

/* Canonical terms, then the checked compact morphism of the reader's
 * profile under the named leaf policy.  The morphism is structural, so its
 * work and continuation depth are bounded by the size and depth of the
 * canonical term.  A document read also returns the span of each input: the
 * spans partition the text, each input running from its first token, or the
 * start of the text, to the next input's first token, or the end of the
 * text. */
static Atom *tptp_reader_read_compact(CettaLibraryContext *ctx, Arena *arena,
                                      Atom *head, Atom **args, uint32_t nargs,
                                      TptpReadSource source) {
    CettaTptpReaderV1 *resource = tptp_reader_arg(ctx, args, nargs, 3u);
    size_t *starts = NULL;
    const char *input = NULL;
    size_t input_len;
    Atom *canonical = NULL;
    Atom *compact = NULL;
    CettaTptpReadOutcomeV1 outcome;
    CettaDeterministicEquationStatusV1 status =
        CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    uint64_t work_used = 0u;
    char error[512] = {0};
    bool read;

    if (!resource)
        return tptp_reader_mismatch(arena, nargs > 0u ? args[0] : NULL);
    if (!atom_is_symbol(args[1], "tagged") &&
        !atom_is_symbol(args[1], "mixed") &&
        !atom_is_symbol(args[1], "native"))
        return tptp_answer(arena, "TPTP:UnknownLeafPolicy", &args[1], 1u);
    if (!cetta_langdef_text_arg(args[2], &input))
        return tptp_error(arena, head,
                          source == TPTP_READ_FILE
                              ? "tptp compact read expects a path"
                              : "tptp compact read expects a string");
    if (!resource->compact) {
        Atom *reason[1] = {atom_string(arena, resource->compact_error)};
        return tptp_answer(arena, "TPTP:CompactMorphismUnavailable", reason, 1u);
    }
    input_len = input ? strlen(input) : 0u;
    switch (source) {
    case TPTP_READ_FILE:
        read = cetta_tptp_prepared_reader_read_file_outcome_v1(
            &resource->reader, input, arena, &canonical, &outcome,
            error, sizeof(error));
        break;
    case TPTP_READ_DOCUMENT:
        read = cetta_tptp_prepared_reader_read_text_spans_v1(
            &resource->reader, input ? input : "", input_len, arena,
            &canonical, &starts, &outcome, error, sizeof(error));
        break;
    default:
        read = cetta_tptp_prepared_reader_read_text_outcome_v1(
            &resource->reader, input ? input : "", input_len, arena,
            &canonical, &outcome, error, sizeof(error));
        break;
    }
    if (!read) {
        free(starts);
        return tptp_read_answer(arena, head, false, NULL, &outcome, error,
                                "tptp compact read failed");
    }
    if (!cetta_tptp_compact_records_v1(
            resource->compact, atom_name_cstr(args[1]), canonical, arena,
            &compact, &work_used, &status, error, sizeof(error))) {
        Atom *parts[2] = {
            atom_symbol(arena, cetta_deterministic_equation_status_name_v1(status)),
            atom_string(arena, error[0] ? error : "compact morphism failed")};
        free(starts);
        return tptp_answer(arena, "TPTP:CompactFailure", parts, 2u);
    }
    if (source == TPTP_READ_DOCUMENT) {
        /* The records are a list; their spans are the parallel list. */
        Atom *const *records = NULL;
        CettaExprLen count = 0u;
        bool records_listed = compact &&
            atom_sequence_view(compact, &records, &count);
        Atom **spans = count
            ? arena_alloc(arena, sizeof(*spans) * (size_t)count) : NULL;
        Atom *parts[2];
        if (!records_listed || (count > 0u && !starts)) {
            free(starts);
            return tptp_error(arena, head, "tptp document read: input spans");
        }
        for (CettaExprLen k = 0u; k < count; k++) {
            size_t start = k == 0u ? 0u : starts[k];
            size_t stop = k + 1u < count ? starts[k + 1u] : input_len;
            Atom *bounds[2] = {atom_int(arena, (int64_t)start),
                               atom_int(arena, (int64_t)stop)};
            spans[k] = tptp_expr(arena, "TPTP:Span", bounds, 2u);
        }
        free(starts);
        parts[0] = compact;
        parts[1] = atom_is_list(compact)
            ? atom_list(arena, spans, count)
            : atom_expr(arena, spans, count);
        return tptp_answer(arena, "TPTP:ReadDocument", parts, 2u);
    }
    free(starts);
    return cetta_langdef_return(arena, compact);
}

static Atom *tptp_reader_read_compact_text(CettaLibraryContext *ctx,
                                           Arena *arena, Atom *head,
                                           Atom **args, uint32_t nargs) {
    return tptp_reader_read_compact(ctx, arena, head, args, nargs,
                                    TPTP_READ_TEXT);
}

static Atom *tptp_reader_read_compact_file(CettaLibraryContext *ctx,
                                           Arena *arena, Atom *head,
                                           Atom **args, uint32_t nargs) {
    return tptp_reader_read_compact(ctx, arena, head, args, nargs,
                                    TPTP_READ_FILE);
}

static Atom *tptp_reader_read_compact_document(CettaLibraryContext *ctx,
                                               Arena *arena, Atom *head,
                                               Atom **args, uint32_t nargs) {
    return tptp_reader_read_compact(ctx, arena, head, args, nargs,
                                    TPTP_READ_DOCUMENT);
}

/* The canonical terms of a file or a text, by a reader. */
static Atom *tptp_reader_read_file(CettaLibraryContext *ctx, Arena *arena,
                                   Atom *head, Atom **args, uint32_t nargs) {
    CettaTptpReaderV1 *resource = tptp_reader_arg(ctx, args, nargs, 2u);
    const char *path = NULL;
    Atom *result = NULL;
    CettaTptpReadOutcomeV1 outcome;
    char error[512] = {0};
    bool read;

    if (!resource)
        return tptp_reader_mismatch(arena, nargs > 0u ? args[0] : NULL);
    if (!cetta_langdef_text_arg(args[1], &path))
        return tptp_error(arena, head, "tptp reader read-file expects a path");
    read = cetta_tptp_prepared_reader_read_file_outcome_v1(
        &resource->reader, path, arena, &result, &outcome, error, sizeof(error));
    return tptp_read_answer(arena, head, read, result, &outcome, error,
                            "tptp reader read-file failed");
}

static Atom *tptp_reader_read_text(CettaLibraryContext *ctx, Arena *arena,
                                   Atom *head, Atom **args, uint32_t nargs) {
    CettaTptpReaderV1 *resource = tptp_reader_arg(ctx, args, nargs, 2u);
    const char *text = NULL;
    Atom *result = NULL;
    CettaTptpReadOutcomeV1 outcome;
    char error[512] = {0};
    bool read;

    if (!resource)
        return tptp_reader_mismatch(arena, nargs > 0u ? args[0] : NULL);
    if (!cetta_langdef_text_arg(args[1], &text))
        return tptp_error(arena, head, "tptp reader read-text expects a string");
    read = cetta_tptp_prepared_reader_read_text_outcome_v1(
        &resource->reader, text ? text : "", text ? strlen(text) : 0u, arena,
        &result, &outcome, error, sizeof(error));
    return tptp_read_answer(arena, head, read, result, &outcome, error,
                            "tptp reader read-text failed");
}

/* The canonical terms of a file or a text, by the frozen reader. */
static Atom *tptp_read_file_frozen(CettaLibraryContext *ctx, Arena *arena,
                                   Atom *head, Atom **args, uint32_t nargs) {
    const char *path = NULL;
    Atom *result = NULL;
    CettaTptpReadOutcomeV1 outcome;
    char error[512] = {0};
    bool read;

    if (nargs != 1u || !cetta_langdef_text_arg(args[0], &path))
        return tptp_error(arena, head, "tptp frozen read-file expects a path");
    read = cetta_tptp_read_file_frozen_outcome_v1(
        path, arena, &result, &outcome, error, sizeof(error));
    return tptp_read_answer(arena, head, read, result, &outcome, error,
                            "tptp frozen read-file failed");
}

static Atom *tptp_read_text_frozen(CettaLibraryContext *ctx, Arena *arena,
                                   Atom *head, Atom **args, uint32_t nargs) {
    const char *text = NULL;
    Atom *result = NULL;
    CettaTptpReadOutcomeV1 outcome;
    char error[512] = {0};
    bool read;

    if (nargs != 1u || !cetta_langdef_text_arg(args[0], &text))
        return tptp_error(arena, head, "tptp frozen read-text expects a string");
    read = cetta_tptp_read_text_frozen_outcome_v1(
        text ? text : "", arena, &result, &outcome, error, sizeof(error));
    return tptp_read_answer(arena, head, read, result, &outcome, error,
                            "tptp frozen read-text failed");
}

typedef Atom *(*TptpOperationV1)(CettaLibraryContext *ctx, Arena *arena,
                                 Atom *head, Atom **args, uint32_t nargs);

static const struct {
    const char *head;
    TptpOperationV1 run;
} tptp_operations[] = {
    {"__cetta_lib_tptp_syntaxbnf_digest_v1", tptp_syntaxbnf_digest},
    {"__cetta_lib_tptp_write_atom_v1", tptp_write_atom},
    {"__cetta_lib_tptp_read_atom_v1", tptp_read_atom},
    {"__cetta_lib_tptp_snapshot_construct_v1", tptp_snapshot_construct},
    {"__cetta_lib_tptp_snapshot_info_v1", tptp_snapshot_info},
    {"__cetta_lib_tptp_reader_open_v1", tptp_reader_open},
    {"__cetta_lib_tptp_reader_info_v1", tptp_reader_info},
    {"__cetta_lib_tptp_reader_artifact_info_v1", tptp_reader_artifact_info},
    {"__cetta_lib_tptp_reader_canonical_print_v1", tptp_reader_canonical_print},
    {"__cetta_lib_tptp_reader_canonical_language_v1",
     tptp_reader_canonical_language},
    {"__cetta_lib_tptp_reader_print_compact_v1", tptp_reader_print_compact},
    {"__cetta_lib_tptp_reader_read_compact_text_v1",
     tptp_reader_read_compact_text},
    {"__cetta_lib_tptp_reader_read_compact_file_v1",
     tptp_reader_read_compact_file},
    {"__cetta_lib_tptp_reader_read_compact_document_v1",
     tptp_reader_read_compact_document},
    {"__cetta_lib_tptp_reader_read_file_v1", tptp_reader_read_file},
    {"__cetta_lib_tptp_reader_read_text_v1", tptp_reader_read_text},
    {"__cetta_lib_tptp_read_file_frozen_v1", tptp_read_file_frozen},
    {"__cetta_lib_tptp_read_text_frozen_v1", tptp_read_text_frozen},
};

Atom *cetta_tptp_module_dispatch(CettaLibraryContext *ctx, Space *space,
                                 Arena *arena, Atom *head, Atom **args,
                                 uint32_t nargs) {
    const char *name;
    (void)space;

    if (!head || head->kind != ATOM_SYMBOL)
        return NULL;
    name = atom_name_cstr(head);
    for (size_t i = 0u; i < sizeof(tptp_operations) / sizeof(tptp_operations[0]); i++) {
        if (strcmp(name, tptp_operations[i].head) == 0)
            return tptp_operations[i].run(ctx, arena, head, args, nargs);
    }
    return NULL;
}
