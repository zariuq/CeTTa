#include "module_value_internal_v1.h"
#include "src/utf8.h"
#include "src/symbol.h"

#include <stdlib.h>
#include <string.h>

static bool string_is(Atom *a, const char *s) {
    return a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING &&
        atom_string_len(a) == strlen(s) && !memcmp(a->ground.sval, s, strlen(s));
}

static Atom *field(Atom *record, const char *key) {
    if (!record || record->kind != ATOM_EXPR || !record->expr.len || !atom_is_symbol(record->expr.elems[0], "Record")) return NULL;
    Atom *found = NULL;
    for (CettaExprIndex i = 1; i < record->expr.len; ++i) {
        Atom *row = record->expr.elems[i];
        if (!module_form_v1(row, "Field", 2u)) return NULL;
        if (string_is(row->expr.elems[1], key)) {
            if (found) return NULL;
            found = row->expr.elems[2];
        }
    }
    return found;
}

static bool record_keys(Atom *record, const char *const *keys, size_t n) {
    if (!record || record->kind != ATOM_EXPR || record->expr.len != n + 1u ||
        !atom_is_symbol(record->expr.elems[0], "Record")) return false;
    for (size_t i = 0; i < n; ++i) if (!field(record, keys[i])) return false;
    return true;
}

static bool declares_envelope(Atom *record) {
    for (CettaExprIndex i = 1; i < record->expr.len; ++i) {
        Atom *row = record->expr.elems[i];
        if (module_form_v1(row, "Field", 2u) && string_is(row->expr.elems[1], "mettail") &&
            string_is(row->expr.elems[2], "module/1")) return true;
    }
    return false;
}

static bool text(Atom *a) { return a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING; }
static bool sequence(Atom *a, const char *head) {
    return a && a->kind == ATOM_EXPR && a->expr.len && atom_is_symbol(a->expr.elems[0], head);
}

static bool visit(ModuleWorkV1 *w) {
    if (!module_spend_v1(w)) return false;
    return w->depth < 512u || module_fail_v1(w, "incomplete", "Portable module structural depth exhausted.");
}

static Atom *named_symbol(ModuleWorkV1 *w, Atom *name) {
    if (!text(name)) return NULL;
    return atom_symbol_id(w->arena, symbol_intern_bytes(g_symbols,
        (const uint8_t *)name->ground.sval, atom_string_len(name)));
}

static Atom *named_application(ModuleWorkV1 *w, Atom *name, ModuleValuesV1 *args) {
    Atom *head = named_symbol(w, name);
    if (!head || args->len > SIZE_MAX / sizeof(Atom *) - 1u) return NULL;
    Atom **fields = arena_alloc(w->arena, (args->len + 1u) * sizeof(*fields));
    if (!fields) return NULL;
    fields[0] = head;
    for (size_t i = 0; i < args->len; ++i) fields[i + 1u] = args->items[i];
    return atom_expr(w->arena, fields, args->len + 1u);
}

static bool natural(Atom *a) {
    if (!a || a->kind != ATOM_GROUNDED) return false;
    if (a->ground.gkind == GV_INT) return a->ground.ival >= 0;
    if (a->ground.gkind != GV_BIGINT) return false;
    const char *p = atom_bigint_cstr(a);
    if (!p || !*p) return false;
    for (; *p; ++p) if (*p < '0' || *p > '9') return false;
    return true;
}

static bool identifier(Atom *a) {
    if (!text(a) || !atom_string_len(a)) return false;
    for (size_t i = 0; i < atom_string_len(a); ++i) {
        unsigned char c = (unsigned char)a->ground.sval[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (i && c >= '0' && c <= '9'))) return false;
    }
    return true;
}

/* Opaque specifications still belong to the complete finite Value carrier.
 * This validation grants no interpretation or execution authority. */
static bool value_valid(ModuleWorkV1 *w, Atom *a) {
    if (!a || !visit(w)) return false;
    if (text(a) || atom_is_symbol(a, "True") || atom_is_symbol(a, "False") || atom_is_symbol(a, "Nil")) return true;
    if (module_form_v1(a, "Integer", 1u) || module_form_v1(a, "NegativeInteger", 1u)) return natural(a->expr.elems[1]);
    if (module_form_v1(a, "FloatBits", 1u)) {
        Atom *bits = a->expr.elems[1];
        if (!natural(bits)) return false;
        if (bits->ground.gkind == GV_INT) return true;
        const char *s = atom_bigint_cstr(bits);
        while (s[0] == '0' && s[1]) ++s;
        return strlen(s) < 20u || (strlen(s) == 20u && strcmp(s, "18446744073709551615") <= 0);
    }
    bool record = sequence(a, "Record"), bytes = sequence(a, "Bytes");
    if (!record && !bytes && !sequence(a, "List")) return false;
    ++w->depth;
    bool ok = true;
    for (CettaExprIndex i = 1; ok && i < a->expr.len; ++i) {
        if (!module_spend_v1(w)) { ok = false; break; }
        Atom *v = a->expr.elems[i];
        if (bytes) ok = v && v->kind == ATOM_GROUNDED && v->ground.gkind == GV_INT && v->ground.ival >= 0 && v->ground.ival < 256;
        else if (record) ok = module_form_v1(v, "Field", 2u) && text(v->expr.elems[1]) && value_valid(w, v->expr.elems[2]);
        else ok = value_valid(w, v);
    }
    --w->depth;
    return ok;
}

/* Normalize only reference spelling for duplicate detection, never resolve an
 * import here. Bare file paths and file: paths name the same reference. */
static const char *reference_path(Atom *uri, bool *registry, size_t *length) {
    if (!text(uri)) return NULL;
    const char *s = uri->ground.sval;
    *length = atom_string_len(uri);
    *registry = *length >= 4u && !memcmp(s, "rho:", 4u);
    if (*registry || (*length >= 5u && !memcmp(s, "file:", 5u))) {
        size_t prefix = *registry ? 4u : 5u;
        *length -= prefix;
        return s + prefix;
    }
    return memchr(s, ':', *length) ? NULL : s;
}

static Atom *codepoints(ModuleWorkV1 *w, Atom *a) {
    if (text(a)) return a;
    if (!module_form_v1(a, "module:codepoints", 1u)) return NULL;
    ModuleValuesV1 points = {0};
    Atom *out = NULL;
    if (!module_unlist_v1(w, a->expr.elems[1], &points) || points.len > SIZE_MAX / 4u) goto done;
    uint8_t *bytes = arena_alloc(w->arena, points.len * 4u + 1u);
    if (!bytes) goto done;
    size_t len = 0;
    for (size_t i = 0; i < points.len; ++i) {
        Atom *p = points.items[i];
        if (!p || p->kind != ATOM_GROUNDED || p->ground.gkind != GV_INT ||
            p->ground.ival < 0 || p->ground.ival > 0x10ffff ||
            !cetta_utf8_scalar_valid((uint32_t)p->ground.ival)) goto done;
        size_t width = cetta_utf8_encode((uint32_t)p->ground.ival, bytes + len);
        if (!width) goto done;
        len += width;
    }
    out = atom_string_n(w->arena, (char *)bytes, len);
done:
    module_clear_v1(&points); return out;
}

static Atom *unescape(ModuleWorkV1 *w, Atom *a) {
    if (!a || !visit(w)) return NULL;
    if (a->kind != ATOM_EXPR) return a;
    if (module_form_v1(a, "module:codepoints", 1u)) return codepoints(w, a);
    if (module_form_v1(a, "module:symbol", 1u)) {
        Atom *name = codepoints(w, a->expr.elems[1]);
        return named_symbol(w, name);
    }
    Atom *name = NULL; ModuleValuesV1 args = {0}, values = {0}; Atom *out = NULL;
    if (module_form_v1(a, "module:application", 2u)) {
        name = codepoints(w, a->expr.elems[1]);
        if (!text(name) || !module_unlist_v1(w, a->expr.elems[2], &args)) goto done;
    } else {
        if (!a->expr.len || !a->expr.elems[0] || a->expr.elems[0]->kind != ATOM_SYMBOL) goto done;
        Atom *head = a->expr.elems[0];
        if (atom_is_symbol(head, "module:codepoints") || atom_is_symbol(head, "module:symbol") || atom_is_symbol(head, "module:application")) goto done;
        name = atom_string_n(w->arena, atom_name_cstr(head), atom_symbol_length(head));
        for (CettaExprIndex i = 1; i < a->expr.len; ++i)
            if (!module_push_v1(w, &args, a->expr.elems[i])) goto done;
    }
    ++w->depth;
    for (size_t i = 0; i < args.len; ++i) {
        Atom *value = unescape(w, args.items[i]);
        if (!module_push_v1(w, &values, value)) { --w->depth; goto done; }
    }
    --w->depth;
    out = named_application(w, name, &values);
done:
    module_clear_v1(&args); module_clear_v1(&values); return out;
}

/* A decoded export is still data. This is Value.Wire.readTerm?, not eval. */
static Atom *quoted_term(ModuleWorkV1 *w, Atom *a) {
    if (!a || !visit(w)) return NULL;
    if (text(a)) return a;
    if (module_form_v1(a, "Integer", 1u)) return natural(a->expr.elems[1]) ? a->expr.elems[1] : NULL;
    if (module_form_v1(a, "Record", 1u)) {
        Atom *name = field(a, "symbol");
        return named_symbol(w, name);
    }
    static const char *keys[] = {"head", "arguments"};
    if (!record_keys(a, keys, 2u)) return NULL;
    Atom *head = field(a, "head"), *args = field(a, "arguments");
    if (!text(head) || !sequence(args, "List")) return NULL;
    ModuleValuesV1 values = {0}; Atom *out = NULL;
    ++w->depth;
    for (CettaExprIndex i = 1; i < args->expr.len; ++i)
        if (!module_push_v1(w, &values, quoted_term(w, args->expr.elems[i]))) goto done;
    out = named_application(w, head, &values);
done:
    --w->depth; module_clear_v1(&values); return out;
}

bool cetta_module_portable_v1_read(Arena *arena, Atom *source, Atom *document,
        uint32_t limit, Atom **out, Atom **refusal) {
    ModuleWorkV1 w = {.arena = arena, .remaining = limit};
    static const char *keys[] = {"dependencies", "exports", "mettail", "name"};
    if (!module_form_v1(document, "module:document", 1u)) goto malformed;
    Atom *value = unescape(&w, document->expr.elems[1]);
    if (!value) goto malformed;
    /* Text is a finite data carrier, not a closed list of executable formats.
     * Retain unknown payloads for other checked interpretations. A module/1
     * envelope additionally asks for its explicit structural schema checks. */
    if (!sequence(value, "Record") || !declares_envelope(value)) goto retained;
    if (!value_valid(&w, value) || !record_keys(value, keys, 4u) || !string_is(field(value, "mettail"), "module/1") ||
        !identifier(field(value, "name")) || !sequence(field(value, "dependencies"), "List") ||
        !sequence(field(value, "exports"), "List")) goto malformed;
    Atom *dependencies = field(value, "dependencies");
    static const char *dependency_keys[] = {"commitment", "uri"};
    for (CettaExprIndex i = 1; i < dependencies->expr.len; ++i) {
        Atom *entry = dependencies->expr.elems[i]; bool registry = false;
        if (!module_spend_v1(&w) || !record_keys(entry, dependency_keys, 2u)) goto malformed;
        size_t path_len = 0;
        const char *path = reference_path(field(entry, "uri"), &registry, &path_len);
        Atom *commitment = field(entry, "commitment");
        if (!path || !path_len || !sequence(commitment, "Bytes") || commitment->expr.len != 33u) goto malformed;
        for (CettaExprIndex j = 1; j < i; ++j) {
            if (!module_spend_v1(&w)) goto malformed;
            bool previous_registry = false;
            size_t previous_len = 0;
            const char *previous = reference_path(field(dependencies->expr.elems[j], "uri"), &previous_registry, &previous_len);
            if (registry == previous_registry && path_len == previous_len && !memcmp(path, previous, path_len)) goto malformed;
        }
    }
    Atom *exports = field(value, "exports");
    if (exports->expr.len < 2u) goto malformed;
    static const char *export_keys[] = {"name", "spec"};
    for (CettaExprIndex i = 1; i < exports->expr.len; ++i) {
        if (!module_spend_v1(&w) || !record_keys(exports->expr.elems[i], export_keys, 2u) ||
            !identifier(field(exports->expr.elems[i], "name"))) goto malformed;
        for (CettaExprIndex j = 1; j < i; ++j) {
            if (!module_spend_v1(&w)) goto malformed;
            if (atom_eq(field(exports->expr.elems[i], "name"), field(exports->expr.elems[j], "name"))) goto malformed;
        }
    }
retained: ;
    Atom *fields[] = {source, value};
    *out = atom_deep_copy(arena, module_make_v1(arena, "ModulePortableValueV1", fields, 2u));
    return *out != NULL;
malformed:
    module_fail_v1(&w, "malformed-portable-module", "Expected one inert module/1 document with unique exports.");
    *refusal = w.refusal; return false;
}

Atom *cetta_module_portable_v1_core(ModuleWorkV1 *w, Atom *module, Atom *name) {
    if (!module_form_v1(module, "ModulePortableValueV1", 2u)) return NULL;
    Atom *payload = module->expr.elems[2];
    if (module_form_v1(payload, "LanguageDefV2", 5u)) return payload;
    if (module_form_v1(payload, "ExtendedLanguageDefV1", 3u) &&
        module_form_v1(payload->expr.elems[2], "LanguageDefV2", 5u)) return payload->expr.elems[2];
    Atom *exports = field(module->expr.elems[2], "exports");
    if (!sequence(exports, "List")) {
        module_fail_v1(w, "unresolved-portable-core-interpretation", "The payload is retained; this method has not established a core interpretation.");
        return NULL;
    }
    for (CettaExprIndex i = 1; i < exports->expr.len; ++i) {
        if (!module_spend_v1(w)) return NULL;
        Atom *entry = exports->expr.elems[i];
        if (!atom_eq(field(entry, "name"), name)) continue;
        Atom *core = quoted_term(w, field(entry, "spec"));
        if (module_form_v1(core, "ExtendedLanguageDefV1", 3u)) core = core->expr.elems[2];
        if (module_form_v1(core, "LanguageDefV2", 5u)) return core;
        module_fail_v1(w, "unresolved-export-interpretation", "The export is retained, but is not an encoded core language.");
        return NULL;
    }
    module_fail_v1(w, "missing-export", "The requested export is absent."); return NULL;
}

static Atom *escape_string(ModuleWorkV1 *w, const uint8_t *s, size_t len) {
    bool plain = true;
    for (size_t i = 0; i < len; ++i)
        if (s[i] < 32u || s[i] >= 127u || s[i] == '"' || s[i] == '\\') plain = false;
    if (plain) return atom_string_n(w->arena, (const char *)s, len);
    ModuleValuesV1 points = {0}; Atom *out = NULL;
    for (size_t at = 0; at < len;) {
        uint32_t cp, width;
        if (!module_spend_v1(w) || !cetta_utf8_decode_one(s, len, at, &cp, &width) ||
            !module_push_v1(w, &points, atom_int(w->arena, cp))) goto done;
        at += width;
    }
    Atom *args[] = {module_list_v1(w->arena, &points)};
    out = module_make_v1(w->arena, "module:codepoints", args, 1u);
done:
    module_clear_v1(&points); return out;
}

static bool safe_symbol(const char *s, size_t len) {
    if (strlen(s) != len) return false;
    if (!*s || *s == '$' || *s == '!' || *s == '+' || *s == '-' || (*s >= '0' && *s <= '9') ||
        !strcmp(s, "True") || !strcmp(s, "False") || !strcmp(s, "true") || !strcmp(s, "false")) return false;
    for (; *s; ++s) if ((unsigned char)*s < 33u || (unsigned char)*s >= 127u || strchr("();\"\\", *s)) return false;
    return true;
}

static Atom *escape_term(ModuleWorkV1 *w, Atom *a) {
    if (!a || !visit(w)) return NULL;
    if (text(a)) return escape_string(w, (const uint8_t *)a->ground.sval, atom_string_len(a));
    if (natural(a)) return a;
    if (a->kind == ATOM_SYMBOL) {
        const char *name = atom_name_cstr(a);
        size_t len = atom_symbol_length(a);
        if (safe_symbol(name, len)) return a;
        Atom *s = escape_string(w, (const uint8_t *)name, len);
        return s ? module_make_v1(w->arena, "module:symbol", &s, 1u) : NULL;
    }
    if (a->kind != ATOM_EXPR || !a->expr.len || a->expr.elems[0]->kind != ATOM_SYMBOL) return NULL;
    const char *head = atom_name_cstr(a->expr.elems[0]);
    size_t head_len = atom_symbol_length(a->expr.elems[0]);
    ModuleValuesV1 args = {0}; Atom *out = NULL;
    ++w->depth;
    for (CettaExprIndex i = 1; i < a->expr.len; ++i)
        if (!module_push_v1(w, &args, escape_term(w, a->expr.elems[i]))) goto done;
    if (safe_symbol(head, head_len) && strcmp(head, "module:codepoints") && strcmp(head, "module:symbol") && strcmp(head, "module:application"))
        out = module_make_v1(w->arena, head, args.items, args.len);
    else {
        Atom *name = escape_string(w, (const uint8_t *)head, head_len);
        Atom *fields[] = {name, module_list_v1(w->arena, &args)};
        if (name) out = module_make_v1(w->arena, "module:application", fields, 2u);
    }
done:
    --w->depth; module_clear_v1(&args); return out;
}

bool cetta_module_portable_v1_write(Arena *arena, Atom *value, uint32_t limit, Atom **out, Atom **refusal) {
    ModuleWorkV1 w = {.arena = arena, .remaining = limit};
    if (module_form_v1(value, "ModulePortableValueV1", 2u)) value = value->expr.elems[2];
    Atom *payload = escape_term(&w, value);
    if (payload) {
        Atom *document = module_make_v1(arena, "module:document", &payload, 1u);
        char *text = atom_to_parseable_string(arena, document);
        if (text) { *out = atom_string(arena, text); return true; }
    }
    module_fail_v1(&w, "unresolved-portable-value", "Portable text requires finite symbol-headed data, natural numbers and Unicode strings.");
    *refusal = w.refusal; return false;
}
