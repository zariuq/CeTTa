#include "library_syntax.h"
#include "eval.h"
#include "library.h"
#include "name_key.h"
#include "parser.h"
#include "petta_program.h"
#include "petta_semantics.h"
#include "string_ops.h"
#include "tensor.h"
#include "term_canon.h"
#include "u64_index.h"
#include "utf8.h"
#include <float.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Atom *fault(Arena *arena, const char *reason) {
    return atom_expr2(arena, atom_symbol(arena, "Syntax:Fault"), atom_symbol(arena, reason));
}

static bool form(Atom *value, const char *head, CettaExprLen fields) {
    return value && value->kind == ATOM_EXPR && value->expr.len == fields + 1u &&
        atom_is_symbol(value->expr.elems[0], head);
}

static bool text(Atom *value) {
    return value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING;
}

static bool clean_text(Atom *value) {
    size_t scalars, bad;
    return text(value) && !memchr(value->ground.sval, 0, atom_string_len(value)) &&
        cetta_utf8_count(value->ground.sval, atom_string_len(value), &scalars, &bad);
}

static Atom *bytes(Arena *arena, const void *data, size_t length) {
    CettaTensor *tensor = NULL;
    if (cetta_tensor_new(CETTA_TENSOR_U8, &length, 1, data, length, &tensor) != CETTA_TENSOR_OK) return NULL;
    Atom *value = cetta_tensor_atom(arena, tensor);
    cetta_tensor_release(tensor);
    return value;
}

static Atom *literal(Arena *arena, const char *kind, const void *data, size_t length) {
    Atom *payload = bytes(arena, data, length);
    return payload ? atom_expr3(arena, atom_symbol(arena, "Syntax:Literal"), atom_symbol(arena, kind), payload) : NULL;
}

static Atom *document(Arena *arena, Atom *source_reader, Atom *forms) {
    Atom *fields[] = {atom_symbol(arena, "Syntax:Document"), atom_string(arena, "ST1"), source_reader, forms};
    return atom_expr(arena, fields, 4);
}

static void big_endian(uint64_t value, unsigned char output[8]) {
    for (size_t i = 0; i < 8; i++) { output[7 - i] = (unsigned char)value; value >>= 8; }
}

static Atom *encode_scalar(Arena *arena, Atom *source) {
    if (source->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr(source);
        return literal(arena, "Symbol", name, strlen(name));
    }
    if (source->kind != ATOM_GROUNDED) return NULL;
    unsigned char payload[8];
    uint64_t bits;
    const char *value;
    switch (source->ground.gkind) {
    case GV_INT:
        big_endian((uint64_t)source->ground.ival, payload);
        return literal(arena, "I64", payload, 8);
    case GV_FLOAT: {
        _Static_assert(sizeof(double) == sizeof(uint64_t) && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
            "syntax F64 requires binary64 storage");
        memcpy(&bits, &source->ground.fval, sizeof bits);
        big_endian(bits, payload);
        return literal(arena, "F64", payload, 8);
    }
    case GV_BOOL:
        payload[0] = source->ground.bval;
        return literal(arena, "Bool", payload, 1);
    case GV_STRING:
        return literal(arena, "String", source->ground.sval, atom_string_len(source));
    case GV_BIGINT:
        value = atom_bigint_cstr(source);
        return value ? literal(arena, "BigInt", value, strlen(value)) : NULL;
    case GV_RATIONAL:
        value = atom_rational_cstr(source);
        return value ? literal(arena, "Rational", value, strlen(value)) : NULL;
    case GV_INTERNAL_TAG:
        if (!cetta_internal_tag_is_list(source->ground.ival)) return NULL;
        big_endian((uint64_t)source->ground.ival, payload);
        return literal(arena, "ListTag", payload, 8);
    default: return NULL;
    }
}

typedef struct { Atom *name, *source; } Name;
typedef struct {
    CettaVarMap variables;
    Name *names;
    size_t capacity;
    Arena views;
    CettaU64Index synthetic;
    const char *error;
} Capture;

static Atom *capture_tree(Arena *, Atom *, Capture *);

static void capture_init(Capture *capture) {
    memset(capture, 0, sizeof(*capture));
    cetta_var_map_init(&capture->variables);
    arena_init_detached(&capture->views);
    arena_set_block_growth(&capture->views, 4096);
    cetta_u64_index_init(&capture->synthetic);
}

static void capture_free(Capture *capture) {
    cetta_var_map_free(&capture->variables);
    cetta_u64_index_free(&capture->synthetic);
    arena_free(&capture->views);
    free(capture->names);
}

static Atom *capture_name(Arena *arena, Atom *variable) {
    if (!variable->name_key) {
        const char *name = atom_name_cstr(variable);
        return name ? atom_expr2(arena, atom_symbol(arena, "Syntax:PlainName"), atom_string(arena, name)) : NULL;
    }
    if (!name_key_is_admissible(variable->name_key)) return NULL;
    Capture key;
    capture_init(&key);
    Atom *tree = capture_tree(arena, variable->name_key, &key);
    bool closed = key.variables.len == 0;
    capture_free(&key);
    return tree && closed ? atom_expr2(arena, atom_symbol(arena, "Syntax:NameKey"), tree) : NULL;
}

/* Only views allocated in this private arena are marked synthetic. A source
 * symbol with exactly the same spelling must still be encoded as source data. */
static bool capture_view(Arena *arena, Atom *source, Atom **view, bool *complete, void *context) {
    Capture *capture = context;
    if (eval_native_interrupted()) { capture->error = "Interrupted"; return false; }
    if (source->structural_facts & ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) {
        capture->error = "NotSourceAtom"; return false;
    }
    uint32_t kind;
    if (cetta_u64_index_find(&capture->synthetic, (uint64_t)(uintptr_t)source, &kind)) {
        *view = kind == 1u ? source : atom_symbol(arena, "Syntax:Expr");
        *complete = kind != 1u;
        return true;
    }
    if (source->kind == ATOM_EXPR) {
        if (!source->expr.len) {
            *view = atom_expr2(arena, atom_symbol(arena, "Syntax:Expr"), atom_expr(arena, NULL, 0));
            *complete = true; return true;
        }
        Atom *children = atom_expr(&capture->views, source->expr.elems, source->expr.len);
        Atom *head = atom_symbol(&capture->views, "Syntax:Expr");
        if (cetta_u64_index_insert(&capture->synthetic, (uint64_t)(uintptr_t)children, 1u) != CETTA_GSLT_U32_INDEX_INSERTED_V1 ||
            (!cetta_u64_index_find(&capture->synthetic, (uint64_t)(uintptr_t)head, &kind) &&
             cetta_u64_index_insert(&capture->synthetic, (uint64_t)(uintptr_t)head, 2u) != CETTA_GSLT_U32_INDEX_INSERTED_V1)) {
            capture->error = "Capacity"; return false;
        }
        *view = atom_expr2(&capture->views, head, children);
        *complete = false;
        return true;
    }
    *complete = true;
    if (source->kind != ATOM_VAR) {
        *view = encode_scalar(arena, source);
        if (!*view) capture->error = "NotSourceAtom";
        return *view != NULL;
    }
    Atom *known = cetta_var_map_lookup(&capture->variables, source->var_id);
    if (known) {
        size_t index = (size_t)known->expr.elems[1]->ground.ival;
        Atom *first = capture->names[index].source;
        if (atom_symbol_current_id(first) != atom_symbol_current_id(source)) {
            capture->error = "InconsistentVariableName"; return false;
        }
        if (first->name_key != source->name_key) {
            Atom *name = capture_name(arena, source);
            if (!first->name_key || !source->name_key || !name || !atom_eq(capture->names[index].name, name)) {
                capture->error = "InconsistentVariableName"; return false;
            }
        }
        *view = known; return true;
    }
    size_t index = capture->variables.len;
    if (index == capture->capacity) {
        size_t next = capture->capacity ? capture->capacity * 2 : 8;
        if (next < capture->capacity || next > SIZE_MAX / sizeof(*capture->names)) { capture->error = "Capacity"; return false; }
        Name *more = realloc(capture->names, next * sizeof(*more));
        if (!more) { capture->error = "Capacity"; return false; }
        capture->names = more; capture->capacity = next;
    }
    Atom *name = capture_name(arena, source);
    *view = atom_expr2(arena, atom_symbol(arena, "Syntax:Variable"), atom_int(arena, (int64_t)index));
    if (!name || !cetta_var_map_add(&capture->variables, source->var_id, *view)) {
        capture->error = "InvalidVariableName"; return false;
    }
    capture->names[index] = (Name){name, source};
    return true;
}

static Atom *capture_tree(Arena *arena, Atom *source, Capture *capture) {
    return atom_deep_copy_observed(arena, source, capture_view, capture);
}

static Atom *capture_form(Arena *arena, Atom *source, const char **error) {
    Capture capture;
    capture_init(&capture);
    Atom *tree = capture_tree(arena, source, &capture);
    Atom *result = NULL;
    if (tree) {
        size_t count = capture.variables.len;
        Atom **names = count ? arena_alloc(arena, count * sizeof(*names)) : NULL;
        for (size_t i = 0; i < count; i++) names[i] = capture.names[i].name;
        result = atom_expr3(arena, atom_symbol(arena, "Syntax:Form"), atom_expr(arena, names, count), tree);
    } else *error = capture.error ? capture.error : "InvalidSource";
    capture_free(&capture);
    return result;
}

static Atom *reader(CettaLibraryContext *context, Arena *arena) {
    const CettaLanguageVersion *version = cetta_language_version_of(context->session.language_id, context->session.profile);
    Atom *fields[] = {atom_symbol(arena, "Syntax:Reader"),
        atom_string(arena, cetta_language_canonical_name(context->session.language_id)),
        atom_string(arena, version ? version->name : ""),
        atom_string(arena, context->session.profile ? context->session.profile->name : "")};
    return atom_expr(arena, fields, 4);
}

static Atom *read_document(CettaLibraryContext *context, Arena *arena, Atom *input) {
    Arena source;
    arena_init_detached(&source);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    AtomId *ids = NULL;
    int complete = 0;
    int count = parse_metta_bytes_prefix_ids((const uint8_t *)input->ground.sval, atom_string_len(input),
        &universe, &ids, &complete, NULL, 0);
    const char *error = count < 0 ? "ParseFailed" : NULL;
    Atom **forms = count > 0 ? arena_alloc(arena, (size_t)count * sizeof(*forms)) : NULL;
    for (int i = 0; !error && i < count; i++) {
        Atom *atom = term_universe_get_atom(&universe, ids[i]);
        forms[i] = atom ? capture_form(arena, atom, &error) : NULL;
        if (!forms[i] && !error) error = "InvalidSource";
    }
    free(ids);
    term_universe_free(&universe);
    arena_free(&source);
    return error ? fault(arena, error) : document(arena, reader(context, arena), atom_expr(arena, forms, count));
}

typedef struct {
    Atom **variables;
    bool *seen;
    size_t count, next;
    const char *error;
} Restore;

static Atom *decode_scalar(Arena *arena, Atom *kind, Atom *value) {
    if (!kind || kind->kind != ATOM_SYMBOL) return NULL;
    CettaTensor *tensor = cetta_tensor_from_atom(value);
    if (!tensor || cetta_tensor_dtype(tensor) != CETTA_TENSOR_U8 || cetta_tensor_rank(tensor) != 1 ||
        cetta_tensor_count(tensor) != cetta_tensor_stored(tensor)) return NULL;
    size_t size = cetta_tensor_count(tensor);
    if (size == SIZE_MAX) return NULL;
    unsigned char *data = malloc(size + 1u);
    if (!data) return NULL;
    CettaTensorStatus copied = cetta_tensor_copy_values(tensor, data, size);
    if (copied == CETTA_TENSOR_ENCODING) {
        copied = CETTA_TENSOR_OK;
        for (size_t i = 0; i < size; i++) {
            bool present; CettaTensorScalar scalar;
            if (eval_native_interrupted()) { copied = CETTA_TENSOR_VALUE; break; }
            copied = cetta_tensor_get(tensor, i, &present, &scalar);
            if (copied != CETTA_TENSOR_OK || !present) { copied = CETTA_TENSOR_VALUE; break; }
            data[i] = scalar.u8;
        }
    }
    data[size] = 0;
    Atom *result = NULL;
    if (copied == CETTA_TENSOR_OK) {
        uint64_t bits = 0;
        if (size == 8) for (size_t i = 0; i < 8; i++) bits = (bits << 8) | data[i];
        if (atom_is_symbol(kind, "I64") && size == 8) {
            int64_t integer; memcpy(&integer, &bits, sizeof integer); result = atom_int(arena, integer);
        } else if (atom_is_symbol(kind, "F64") && size == 8) {
            double real; memcpy(&real, &bits, sizeof real); result = atom_float(arena, real);
        } else if (atom_is_symbol(kind, "Bool") && size == 1 && data[0] <= 1) result = atom_bool(arena, data[0]);
        else if (atom_is_symbol(kind, "String")) result = atom_string_n(arena, (char *)data, size);
        else if (atom_is_symbol(kind, "Symbol")) {
            size_t scalars, bad;
            if (size <= UINT32_MAX && !memchr(data, 0, size) && cetta_utf8_count((char *)data, size, &scalars, &bad))
                result = atom_symbol_id(arena, symbol_intern_bytes(g_symbols, data, (uint32_t)size));
        } else if (atom_is_symbol(kind, "BigInt") || atom_is_symbol(kind, "Rational")) {
            if (!memchr(data, 0, size)) {
                bool rational = atom_is_symbol(kind, "Rational");
                char *canonical = rational ? cetta_rational_canonicalize_owned((char *)data) : cetta_bigint_canonicalize_owned((char *)data);
                if (canonical && strlen(canonical) == size && !memcmp(canonical, data, size))
                    result = rational ? atom_rational(arena, canonical) : atom_bigint(arena, canonical);
                if (result && result->ground.gkind != (rational ? GV_RATIONAL : GV_BIGINT)) result = NULL;
                free(canonical);
            }
        } else if (atom_is_symbol(kind, "ListTag") && size == 8 && cetta_internal_tag_is_list((int64_t)bits))
            result = atom_internal_tag(arena, (int64_t)bits);
    }
    free(data);
    return result;
}

static bool restore_view(Arena *arena, Atom *source, Atom **view, bool *complete, void *context) {
    Restore *restore = context;
    if (eval_native_interrupted()) { restore->error = "Interrupted"; return false; }
    if (form(source, "Syntax:Expr", 1) && source->expr.elems[1]->kind == ATOM_EXPR) {
        *view = source->expr.elems[1]; *complete = false; return true;
    }
    *complete = true;
    if (form(source, "Syntax:Literal", 2)) {
        *view = decode_scalar(arena, source->expr.elems[1], source->expr.elems[2]);
        if (!*view) restore->error = "InvalidLiteral";
        return *view != NULL;
    }
    if (form(source, "Syntax:Variable", 1)) {
        Atom *index = source->expr.elems[1];
        if (index->kind == ATOM_GROUNDED && index->ground.gkind == GV_INT && index->ground.ival >= 0 &&
            (uint64_t)index->ground.ival < restore->count) {
            size_t n = (size_t)index->ground.ival;
            if (!restore->seen[n]) {
                if (n != restore->next) { restore->error = "NoncanonicalVariables"; return false; }
                restore->seen[n] = true; restore->next++;
            }
            *view = restore->variables[n]; return true;
        }
        restore->error = "InvalidVariable"; return false;
    }
    restore->error = "InvalidTerm";
    return false;
}

static Atom *restore_form(Arena *arena, Atom *input, const char **error) {
    if (!form(input, "Syntax:Form", 2) || input->expr.elems[1]->kind != ATOM_EXPR) { *error = "InvalidForm"; return NULL; }
    Atom *names = input->expr.elems[1];
    size_t count = names->expr.len;
    if (count > SIZE_MAX / sizeof(Atom *)) { *error = "Capacity"; return NULL; }
    Restore restore = {.count = count};
    restore.variables = count ? calloc(count, sizeof(*restore.variables)) : NULL;
    restore.seen = count ? calloc(count, sizeof(*restore.seen)) : NULL;
    if (count && (!restore.variables || !restore.seen)) { *error = "Capacity"; goto done; }
    for (size_t i = 0; i < count; i++) {
        Atom *name = names->expr.elems[i], *variable = NULL;
        VarId id;
        if (!fresh_var_id_try(&id)) { *error = "Capacity"; goto done; }
        if (form(name, "Syntax:PlainName", 1) && clean_text(name->expr.elems[1]))
            variable = atom_var_with_id(arena, name->expr.elems[1]->ground.sval, id);
        else if (form(name, "Syntax:NameKey", 1)) {
            Restore closed = {0};
            Atom *key = atom_deep_copy_observed(arena, name->expr.elems[1], restore_view, &closed);
            if (key && name_key_is_admissible(key)) variable = atom_var_with_name_key(arena, key, id);
        }
        if (!variable) { *error = "InvalidVariableName"; goto done; }
        restore.variables[i] = variable;
    }
    {
        Atom *result = atom_deep_copy_observed(arena, input->expr.elems[2], restore_view, &restore);
        if (!result) *error = restore.error ? restore.error : "InvalidTerm";
        else if (restore.next != count) { result = NULL; *error = "UnusedVariable"; }
        free(restore.variables); free(restore.seen);
        return result;
    }
done:
    free(restore.variables); free(restore.seen); return NULL;
}

static bool reader_shape(Atom *value) {
    return form(value, "Syntax:Reader", 3) && clean_text(value->expr.elems[1]) &&
        clean_text(value->expr.elems[2]) && clean_text(value->expr.elems[3]);
}

typedef struct {
    const CettaStructuralClass *type;
    atomic_size_t references;
    Arena arena;
    Atom *source_reader;
    Atom **forms;
    size_t count;
} SourceSyntax;

/* Every internal variable belongs to a fresh namespace owned by this quoted
 * source. None is a logical variable of the enclosing host value. */

static const CettaStructuralClass source_type;

static void source_retain(void *value) {
    SourceSyntax *source = value;
    atomic_fetch_add_explicit(&source->references, 1, memory_order_relaxed);
}

static void source_release(void *value) {
    SourceSyntax *source = value;
    if (atomic_fetch_sub_explicit(&source->references, 1, memory_order_acq_rel) != 1) return;
    arena_free(&source->arena); free(source);
}

static SourceSyntax *source_from_atom(Atom *value) {
    return atom_is_structural(value) && cetta_structural_class(value->ground.ptr) == &source_type
        ? value->ground.ptr : NULL;
}

static Atom *restore_document(Arena *arena, Atom *document) {
    if (!form(document, "Syntax:Document", 3) || !text(document->expr.elems[1]) ||
        atom_string_len(document->expr.elems[1]) != 3 || memcmp(document->expr.elems[1]->ground.sval, "ST1", 3) ||
        !reader_shape(document->expr.elems[2]) ||
        document->expr.elems[3]->kind != ATOM_EXPR) return fault(arena, "InvalidDocument");
    Atom *forms = document->expr.elems[3];
    if (forms->expr.len > SIZE_MAX / sizeof(Atom *)) return fault(arena, "Capacity");
    SourceSyntax *source = calloc(1, sizeof(*source));
    if (!source) return fault(arena, "Capacity");
    source->type = &source_type;
    atomic_init(&source->references, 1);
    arena_init_detached(&source->arena);
    arena_set_block_growth(&source->arena, 4096);
    source->source_reader = atom_deep_copy(&source->arena, document->expr.elems[2]);
    source->count = forms->expr.len;
    source->forms = source->count ? arena_alloc(&source->arena, source->count * sizeof(*source->forms)) : NULL;
    const char *error = NULL;
    for (size_t i = 0; !error && i < forms->expr.len; i++) source->forms[i] = restore_form(&source->arena, forms->expr.elems[i], &error);
    Atom *result = error ? fault(arena, error) : atom_structural(arena, source);
    source_release(source);
    return result;
}

static Atom *freeze_source(Arena *arena, const SourceSyntax *source) {
    Atom **forms = source->count ? arena_alloc(arena, source->count * sizeof(*forms)) : NULL;
    const char *error = NULL;
    for (size_t i = 0; !error && i < source->count; i++) forms[i] = capture_form(arena, source->forms[i], &error);
    return error ? fault(arena, error) : document(arena, atom_deep_copy(arena, source->source_reader), atom_expr(arena, forms, source->count));
}

/* A restored source has fresh variable identities. Identity equality is not
 * portable syntax equality; freeze supplies that complete observation. */
static bool source_equal(const void *a, const void *b) { return a == b; }
static int source_compare(const void *a, const void *b) { return (uintptr_t)a < (uintptr_t)b ? -1 : a != b; }
static uint64_t source_hash(const void *value) { return (uint64_t)(uintptr_t)value; }
static size_t source_display(const void *value, char *buffer, size_t size) {
    return (size_t)snprintf(buffer, size, "(SourceSyntax forms:%zu)", ((const SourceSyntax *)value)->count);
}
static Atom *source_inspect(const void *value, Arena *arena) { return freeze_source(arena, value); }
static size_t source_bytes(const void *value) {
    const SourceSyntax *source = value;
    size_t total = sizeof(*source);
    size_t fields[] = {source->arena.reserved_bytes, source->arena.external_bytes, source->arena.persistent_bytes};
    for (size_t i = 0; i < sizeof(fields) / sizeof(*fields); i++) {
        if (fields[i] > SIZE_MAX - total) return SIZE_MAX;
        total += fields[i];
    }
    return total;
}
static const CettaStructuralClass source_type = {"SourceSyntax", source_retain, source_release, source_equal,
    source_compare, source_hash, source_display, source_inspect, source_bytes};

static Atom *write_document(CettaLibraryContext *context, Arena *arena, Atom *document) {
    Atom *restored = restore_document(arena, document);
    SourceSyntax *source = source_from_atom(restored);
    if (!source) return restored;
    if (!atom_eq(document->expr.elems[2], reader(context, arena))) return fault(arena, "ReaderChanged");
    size_t count = source->count, total = 0;
    if (count > SIZE_MAX / sizeof(char *) || count > SIZE_MAX / sizeof(size_t)) return fault(arena, "Capacity");
    char **parts = count ? calloc(count, sizeof(*parts)) : NULL;
    size_t *lengths = count ? calloc(count, sizeof(*lengths)) : NULL;
    if (count && (!parts || !lengths)) { free(parts); free(lengths); return fault(arena, "Capacity"); }
    const char *error = NULL;
    for (size_t i = 0; !error && i < count; i++) {
        if (parser_universal_name_syntax_enabled())
            parts[i] = parser_render_syntax_bytes(arena, source->forms[i], PARSER_SYNTAX_PRINT_COMPACT, &lengths[i]);
        else parts[i] = atom_to_source_bytes(arena, source->forms[i],
            context->session.language_id == CETTA_LANGUAGE_PETTA, &lengths[i]);
        if (!parts[i]) error = "NotRenderable";
        else if (lengths[i] >= SIZE_MAX - total) error = "Capacity";
        else total += lengths[i] + 1u;
    }
    Atom *output = NULL;
    if (!error && total == SIZE_MAX) error = "Capacity";
    if (!error) {
        char *joined = malloc(total + 1u);
        if (!joined) error = "Capacity";
        else {
            size_t at = 0;
            for (size_t i = 0; i < count; i++) {
                memcpy(joined + at, parts[i], lengths[i]); at += lengths[i];
                /* The source reader represents a runnable form as ! followed
                 * by its payload; a newline between them ends that form. */
                if (!atom_is_symbol(source->forms[i], "!") || i + 1u == count)
                    joined[at++] = '\n';
            }
            joined[at] = 0;
            output = atom_string_n(arena, joined, at); free(joined);
            Atom *roundtrip = read_document(context, arena, output);
            if (!atom_eq(document, roundtrip)) error = "NotRoundtrippable";
        }
    }
    free(parts); free(lengths);
    return error ? fault(arena, error) : output;
}

bool cetta_syntax_module_call(CettaLibraryContext *context, struct Space *space,
        Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *out) {
    (void)space;
    if (!context || !head || head->kind != ATOM_SYMBOL || count != 1) return false;
    const char *name = atom_name_cstr(head);
    Atom *value;
    if (!strcmp(name, "syntax:read")) { if (!text(arguments[0])) return false; value = read_document(context, arena, arguments[0]); }
    else if (!strcmp(name, "syntax:restore")) value = restore_document(arena, arguments[0]);
    else if (!strcmp(name, "syntax:freeze")) {
        SourceSyntax *source = source_from_atom(arguments[0]);
        if (!source) return false;
        value = freeze_source(arena, source);
    }
    else if (!strcmp(name, "syntax:write")) value = write_document(context, arena, arguments[0]);
    else if (!strcmp(name, "syntax:intrinsic-head?")) {
        /* Conservative PeTTa head classification, independent of whether an
         * optional provider is loaded. It grants no purity or value evidence. */
        bool intrinsic = arguments[0] && arguments[0]->kind == ATOM_SYMBOL &&
            petta_program_head_is_intrinsic(atom_symbol_current_id(arguments[0]));
        value = eval_current_language_id() == CETTA_LANGUAGE_PETTA
            ? petta_semantics_boolean_value(arena, intrinsic) : atom_bool(arena, intrinsic);
    }
    else return false;
    *out = eval_native_interrupted() ? cetta_call_interrupted() : cetta_call_completed_value(value);
    return true;
}
