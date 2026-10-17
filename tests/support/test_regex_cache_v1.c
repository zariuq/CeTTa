/* Exercise the private memo at its actual compilation boundary without
 * exporting cache publication as a language operation. */
#define CETTA_RE_REGISTRY_CONTROLS 1
#include "../../src/library_re.c"

#include <stdio.h>

static unsigned cache_passed, cache_failed;
static void cache_check(bool ok, const char *name) {
    if (ok) ++cache_passed; else { ++cache_failed; fprintf(stderr, "FAIL: %s\n", name); }
}
static Atom *cache_call(CettaLibraryContext *context, Arena *arena, const char *operation,
                        Atom *pattern, Atom *groups) {
    Atom *tuple = atom_expr(arena, (Atom *[]){pattern, groups}, groups ? 2u : 1u);
    Atom *arguments[] = {atom_symbol(arena, operation), tuple}; CettaCallOutcome out = {0};
    bool handled = cetta_re_call(context, arena, atom_symbol(arena, "__cetta_lib_re"), arguments, 2u, &out);
    cache_check(handled && out.kind == CETTA_CALL_VALUE && out.term,
        "actual compilation boundary returns an observation"); return out.term;
}
static RePatternOwner *cache_owner(CettaLibraryContext *context, Atom *handle, uint64_t *id) {
    return cetta_native_handle_arg(handle, pattern_kind, id) ?
        cetta_native_handle_get(context, pattern_kind, *id) : NULL;
}
static Atom *cache_literal(Arena *a, uint32_t scalar) {
    return atom_expr2(a, atom_symbol(a, "re:literal"), atom_int(a, scalar));
}
static void cache_independent_owners(void) {
    CettaLibraryContext context = {.native_handle_next_id = 1u}; Arena keys, first, second;
    arena_init_detached(&keys); arena_init_detached(&first); arena_init_detached(&second);
    Atom *pattern = cache_literal(&keys, 'a');
    Atom *one = cache_call(&context, &first, "compile-value", pattern, NULL);
    Atom *two = cache_call(&context, &second, "compile-value", pattern, NULL);
    uint64_t id_one = 0, id_two = 0;
    RePatternOwner *a = cache_owner(&context, one, &id_one), *b = cache_owner(&context, two, &id_two);
    if (!a || !b) {
        fprintf(stderr, "cache ingress: "); atom_print(one, stderr);
        fprintf(stderr, " / "); atom_print(two, stderr); fprintf(stderr, "\n");
    }
    cache_check(a && b && id_one != id_two && a->program == b->program &&
        a->entry == b->entry && a->entry && a->entry->references == 2u,
        "equal compilations share the program but retain independent close authority");
    if (!a || !b) { arena_free(&first); arena_free(&second); arena_free(&keys);
        cetta_native_handle_cleanup_all(&context); return; }
    cache_check(cetta_native_handle_close(&context, pattern_kind, id_one) &&
        cetta_native_handle_get(&context, pattern_kind, id_two) == b && b->entry->references == 1u,
        "closing one compiled owner does not invalidate another");
    arena_free(&first); arena_free(&keys);
    CettaReResultV1 answer = cetta_re_v1_find(b->program, (const uint8_t *)"a", 1u,
        0u, CETTA_RE_V1_FULL, 0u);
    cache_check(answer.outcome == CETTA_RE_V1_FOUND,
        "shared program and detached memo key survive original source-owner teardown");
    cetta_re_v1_result_free(&answer);
    cetta_native_handle_cleanup_all(&context);
    cache_check(!prepared_length && !prepared_buckets,
        "context teardown clears weak entries even while value holders survive");
    arena_free(&second);
    cache_check(!prepared_length && !prepared_buckets, "late holder release cannot double-retire memo entry");
}
static void cache_growth_and_isolation(void) {
    CettaLibraryContext context = {.native_handle_next_id = 1u},
        other = {.native_handle_next_id = 1u}; Arena keys, values, other_values;
    arena_init_detached(&keys); arena_init_detached(&values); arena_init_detached(&other_values);
    Atom *same_pattern = NULL; CettaReProgramV1 *same_program = NULL;
    for (unsigned i = 0; i < 100u; ++i) {
        Atom *pattern = cache_literal(&keys, 256u + i);
        Atom *handle = cache_call(&context, &values, "compile-value", pattern, NULL);
        uint64_t id = 0; RePatternOwner *owner = cache_owner(&context, handle, &id);
        cache_check(owner && owner->entry, "each admitted unique live program retains its weak memo entry");
        if (!i && owner) { same_pattern = pattern; same_program = owner->program; }
    }
    cache_check(prepared_length == 100u && context.native_handle_len >= 100u,
        "memo and owner registry grow beyond 64 live patterns without a default ceiling");
    Atom *handle = cache_call(&other, &other_values, "compile-value", same_pattern, NULL);
    uint64_t id = 0; RePatternOwner *owner = cache_owner(&other, handle, &id);
    cache_check(owner && owner->program != same_program && prepared_length == 101u,
        "distinct compiler contexts do not accidentally share memo assumptions");
    arena_free(&keys); arena_free(&values);
    cache_check(prepared_length == 1u, "last owner release drops otherwise unreachable programs");
    arena_free(&other_values);
    cache_check(!prepared_length && !prepared_buckets, "weak cache has no retained-program leak");
    cetta_native_handle_cleanup_all(&context); cetta_native_handle_cleanup_all(&other);
}
static Atom *cache_layout(Arena *arena, const char *name) {
    return atom_expr3(arena, atom_symbol(arena, "LCons"),
        atom_expr3(arena, atom_symbol(arena, "re:group"), atom_int(arena, 1), atom_string(arena, name)),
        atom_symbol(arena, "LNil"));
}
static void cache_layout_and_declines(void) {
    CettaLibraryContext context = {.native_handle_next_id = 1u}; Arena arena; arena_init_detached(&arena);
    Atom *pattern = atom_expr3(&arena, atom_symbol(&arena, "re:capture"),
        atom_int(&arena, 1), cache_literal(&arena, 'a'));
    Atom *one = cache_call(&context, &arena, "compile-source", pattern, cache_layout(&arena, "first"));
    Atom *two = cache_call(&context, &arena, "compile-source", pattern, cache_layout(&arena, "second"));
    Atom *three = cache_call(&context, &arena, "compile-value", pattern, NULL);
    uint64_t id; RePatternOwner *a = cache_owner(&context, one, &id),
        *b = cache_owner(&context, two, &id), *c = cache_owner(&context, three, &id);
    cache_check(a && b && c && a->program != b->program && a->program != c->program &&
        !strcmp(cetta_re_v1_group_name(a->program, 1u), "first") &&
        !strcmp(cetta_re_v1_group_name(b->program, 1u), "second") &&
        !cetta_re_v1_group_name(c->program, 1u), "capture layout and absent layout are part of the memo key");
    size_t before = prepared_length;
    Atom *malformed = cache_call(&context, &arena, "compile-source", pattern, atom_int(&arena, 1));
    cache_check(malformed && malformed->kind == ATOM_EXPR && atom_is_symbol(malformed->expr.elems[0], "re:failed") &&
        prepared_length == before, "malformed layout never publishes a cache association");
    Atom *unknown = cache_call(&context, &arena, "compile-value", atom_symbol(&arena, "unknown-regex-constructor"), NULL);
    cache_check(unknown && unknown->kind == ATOM_EXPR && atom_is_symbol(unknown->expr.elems[0], "re:interpretation-needed") &&
        prepared_length == before, "uninterpreted pattern never becomes a fabricated prepared hit");
    arena_free(&arena); cetta_native_handle_cleanup_all(&context);
    cache_check(!prepared_length && !prepared_buckets, "all layout-specific owners clean up");
}
/* Description sharing: an equal description (text and look-behind policy)
 * borrows a live preparation; its last owner retires its descriptions. */
static Atom *text_call(CettaLibraryContext *context, Arena *arena, Atom *text, const char *policy) {
    Atom *rule = atom_expr2(arena, atom_symbol(arena, "re:lookbehind-policy"), atom_symbol(arena, policy));
    Atom *tuple = atom_expr(arena, (Atom *[]){text, rule}, 2u);
    Atom *arguments[] = {atom_symbol(arena, "compile-text"), tuple}; CettaCallOutcome out = {0};
    bool handled = cetta_re_call(context, arena, atom_symbol(arena, "__cetta_lib_re"), arguments, 2u, &out);
    cache_check(handled && out.kind == CETTA_CALL_VALUE && out.term,
        "textual compilation boundary returns an observation"); return out.term;
}
static Atom *text_compile(CettaLibraryContext *context, Arena *arena, const char *text, const char *policy) {
    return text_call(context, arena, atom_string(arena, text), policy);
}
static bool owner_finds(RePatternOwner *owner, const char *text, size_t start, size_t end) {
    if (!owner) return false;
    CettaReResultV1 answer = cetta_re_v1_find(owner->program, (const uint8_t *)text, strlen(text),
        0u, CETTA_RE_V1_SEARCH, 0u);
    bool ok = answer.outcome == CETTA_RE_V1_FOUND && answer.groups[0].start == start && answer.groups[0].end == end;
    cetta_re_v1_result_free(&answer); return ok;
}
static bool headed_value(Atom *value, const char *head) {
    return value && value->kind == ATOM_EXPR && value->expr.len >= 1u && atom_is_symbol(value->expr.elems[0], head);
}
static void cache_description_sharing(void) {
    static CettaLibraryContext context; cetta_library_context_init(&context);
    Arena one, two, three, four; arena_init_detached(&one); arena_init_detached(&two);
    arena_init_detached(&three); arena_init_detached(&four);
    uint64_t runs = frontend_runs, id = 0, id_one = 0;
    RePatternOwner *a = cache_owner(&context, text_compile(&context, &one, "(?x)a|ab", "Forward"), &id_one);
    cache_check(a && a->entry && frontend_runs == runs + 1u && alias_length == 1u && a->entry->references == 1u,
        "a first description runs the frontend once and names its live preparation");
    RePatternOwner *b = cache_owner(&context, text_compile(&context, &two, "(?x)a|ab", "Forward"), &id);
    cache_check(b && a && b != a && id != id_one && b->program == a->program && b->entry == a->entry &&
        a->entry->references == 2u && frontend_runs == runs + 1u,
        "an equal description borrows the live preparation without reading the text again");
    RePatternOwner *c = cache_owner(&context, text_compile(&context, &three, "(?x)a|ab", "Reverse"), &id);
    cache_check(c && a && frontend_runs == runs + 2u && c->entry == a->entry && alias_length == 2u &&
        a->entry->references == 3u, "the look-behind policy is part of a description; equal semantics share one entry");
    RePatternOwner *d = cache_owner(&context, text_compile(&context, &four, "(?x) a | ab", "Forward"), &id);
    cache_check(d && a && frontend_runs == runs + 3u && d->entry == a->entry && alias_length == 3u,
        "a description with the same lexical reading is read once and joins the entry");
    cache_check(owner_finds(b, "ab", 0u, 1u) && owner_finds(d, "ab", 0u, 1u),
        "borrowed preparations keep ordered-alternative semantics");
    cache_check(cetta_native_handle_close(&context, pattern_kind, id_one) && b && b->entry &&
        b->entry->references == 3u && alias_length == 3u, "closing one owner keeps the entry and its descriptions");
    Arena again; arena_init_detached(&again);
    RePatternOwner *e = cache_owner(&context, text_compile(&context, &again, "(?x)a|ab", "Forward"), &id);
    cache_check(e && b && e->program == b->program && frontend_runs == runs + 3u,
        "a description still names its entry after an unrelated owner closes");
    arena_free(&two); arena_free(&three); arena_free(&four); arena_free(&again); arena_free(&one);
    cache_check(!prepared_length && !prepared_buckets && !alias_length && !alias_buckets,
        "the last owner's release retires the entry with every description of it");
    Arena fresh; arena_init_detached(&fresh);
    RePatternOwner *f = cache_owner(&context, text_compile(&context, &fresh, "a|ab", "Forward"), &id);
    cache_check(f && frontend_runs == runs + 4u && alias_length == 1u,
        "a retired description is read again rather than borrowed");
    arena_free(&fresh);
    cetta_library_context_free(&context);
    cache_check(!prepared_length && !alias_length && !alias_buckets, "context teardown leaves no description registry");
}
static void cache_description_declines(void) {
    static CettaLibraryContext context; cetta_library_context_init(&context);
    Arena arena; arena_init_detached(&arena);
    uint64_t runs = frontend_runs;
    Atom *open = text_compile(&context, &arena, "(", "Forward");
    Atom *again = text_compile(&context, &arena, "(", "Forward");
    cache_check(headed_value(open, "re:source-observation") && headed_value(again, "re:source-observation") &&
        frontend_runs == runs + 2u && !alias_length && !prepared_length,
        "a rejected description publishes nothing and is read on every use");
    Atom *escape = text_compile(&context, &arena, "\\q", "Forward");
    cache_check(headed_value(escape, "re:interpretation-needed") && !alias_length && !prepared_length,
        "an uninterpreted description never becomes a borrowed preparation");
    runs = frontend_runs;
    Atom *policy = text_compile(&context, &arena, "a", "Sideways");
    cache_check(headed_value(policy, "re:failed") && frontend_runs == runs && !alias_length,
        "an invalid policy fails before the frontend and publishes nothing");
    Atom *number = text_call(&context, &arena, atom_int(&arena, 42), "Forward");
    cache_check(headed_value(number, "re:source-fault") && frontend_runs == runs && !alias_length,
        "non-text descriptions are source faults, not cache keys");
    arena_free(&arena); cetta_library_context_free(&context);
    cache_check(!prepared_length && !alias_length, "declined descriptions leave no registry state");
}
static void cache_description_growth(void) {
    static CettaLibraryContext context, other; cetta_library_context_init(&context); cetta_library_context_init(&other);
    Arena first, second, isolated; arena_init_detached(&first); arena_init_detached(&second); arena_init_detached(&isolated);
    uint64_t runs = frontend_runs; char text[32]; bool all = true;
    for (unsigned i = 0; i < 300u; ++i) {
        snprintf(text, sizeof(text), "k%03u|z", i);
        uint64_t id; all = all && cache_owner(&context, text_compile(&context, &first, text, "Forward"), &id) != NULL;
    }
    cache_check(all && alias_length == 300u && prepared_length == 300u && frontend_runs == runs + 300u &&
        alias_capacity >= 512u && !(alias_capacity & (alias_capacity - 1u)),
        "the description registry grows past every fixed size without a ceiling");
    for (unsigned i = 0; i < 300u; ++i) {
        snprintf(text, sizeof(text), "k%03u|z", i);
        uint64_t id; RePatternOwner *owner = cache_owner(&context, text_compile(&context, &second, text, "Forward"), &id);
        all = all && owner && owner->entry && owner->entry->references == 2u;
    }
    cache_check(all && frontend_runs == runs + 300u, "every live description is borrowed on its second use");
    uint64_t id; RePatternOwner *elsewhere = cache_owner(&other, text_compile(&other, &isolated, "k000|z", "Forward"), &id);
    cache_check(elsewhere && frontend_runs == runs + 301u && prepared_length == 301u,
        "another context reads its own descriptions");
    arena_free(&first);
    cache_check(alias_length == 301u && prepared_length == 301u, "releasing one holder keeps borrowed descriptions");
    arena_free(&second);
    cache_check(alias_length == 1u && prepared_length == 1u, "releasing the last holders retires their descriptions");
    arena_free(&isolated);
    cache_check(!alias_length && !alias_buckets && !prepared_length && !prepared_buckets,
        "a drained registry frees its tables");
    cetta_library_context_free(&context); cetta_library_context_free(&other);
}
static unsigned refusal_target, refusal_count;
static bool refuse_target(void) { return ++refusal_count == refusal_target; }
static bool refuse_all(void) { return true; }
static void cache_description_faults(void) {
    static CettaLibraryContext context; cetta_library_context_init(&context);
    Arena arena; arena_init_detached(&arena);
    uint64_t runs = frontend_runs, id;
    registry_refuse = refuse_all;
    RePatternOwner *a = cache_owner(&context, text_compile(&context, &arena, "a|ab", "Forward"), &id);
    RePatternOwner *b = cache_owner(&context, text_compile(&context, &arena, "a|ab", "Forward"), &id);
    registry_refuse = NULL;
    cache_check(a && b && !a->entry && !b->entry && a->program != b->program && frontend_runs == runs + 2u &&
        !alias_length && !prepared_length && owner_finds(a, "ab", 0u, 1u) && owner_finds(b, "ab", 0u, 1u),
        "refused registry storage leaves correct, independent, unshared owners");
    arena_free(&arena);
    /* Refuse each registry allocation of a first description in turn. */
    for (unsigned target = 1u; target <= 4u; ++target) {
        Arena held; arena_init_detached(&held);
        refusal_target = target; refusal_count = 0u; registry_refuse = refuse_target;
        runs = frontend_runs;
        RePatternOwner *first = cache_owner(&context, text_compile(&context, &held, "x|xy", "Forward"), &id);
        unsigned seen = refusal_count;
        RePatternOwner *second = cache_owner(&context, text_compile(&context, &held, "x|xy", "Forward"), &id);
        registry_refuse = NULL;
        /* Refusing the entry table or entry leaves the first owner unshared;
         * refusing its description or the description table leaves only
         * the text unshared. Either way the second compilation registers. */
        cache_check(first && second && owner_finds(first, "xy", 0u, 1u) && owner_finds(second, "xy", 0u, 1u) &&
            seen == target && frontend_runs == runs + 2u && alias_length == 1u &&
            (target >= 3u) == (second->program == first->program),
            "a refusal at any registry allocation keeps both compilations correct");
        arena_free(&held);
        cache_check(!alias_length && !alias_buckets && !prepared_length && !prepared_buckets,
            "after a refused registration the registry still drains completely");
    }
    cetta_library_context_free(&context);
}
int main(void) {
    SymbolTable symbols; VarInternTable variables;
    symbol_table_init(&symbols); g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables); g_var_intern = &variables;
    cache_independent_owners(); cache_growth_and_isolation(); cache_layout_and_declines();
    cache_description_sharing(); cache_description_declines(); cache_description_growth();
    cache_description_faults();
    g_var_intern = NULL; var_intern_free(&variables); symbol_table_free(&symbols); g_symbols = NULL;
    printf("(RegexCacheV1Summary %u %u)\n", cache_passed, cache_failed); return cache_failed ? 1 : 0;
}
