#include "regex_class_source_v1.h"
#include "unicode_class_v1.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const CettaReClassSourceOptionsV1 *options;
    Atom **problem;
} ClassSource;

static const char *label(Atom *tree) {
    if (!tree || tree->kind != ATOM_EXPR || tree->expr.len < 4u ||
        !atom_is_symbol(tree->expr.elems[0], "CstRuleV1")) return NULL;
    Atom *name = tree->expr.elems[1];
    return name && name->kind == ATOM_GROUNDED && name->ground.gkind == GV_STRING &&
        !memchr(name->ground.sval, 0, atom_string_len(name)) ? name->ground.sval : NULL;
}
static Atom *child(Atom *tree, size_t index) {
    return tree && tree->kind == ATOM_EXPR && tree->expr.len > index + 4u ? tree->expr.elems[index + 4u] : NULL;
}
static bool arity(Atom *tree, size_t count) { return label(tree) && tree->expr.len == count + 4u; }
static bool fail(ClassSource *source, Atom *at, const char *kind, const char *reason) {
    if (!*source->problem) {
        Arena *arena = source->options->arena;
        *source->problem = atom_expr(arena, (Atom *[]){atom_symbol(arena, kind), atom_string(arena, reason),
            label(at) ? at->expr.elems[2] : atom_int(arena, 0),
            label(at) ? at->expr.elems[3] : atom_int(arena, 0)}, 4u);
    }
    return false;
}
static bool take(ClassSource *source, Atom *at) {
    if (!source->options->bounded) return true;
    if (!source->options->remaining || !*source->options->remaining)
        return fail(source, at, "re:incomplete", "class projection work bound");
    --*source->options->remaining; return true;
}
static bool allocation(ClassSource *source, Atom *at, UnicodeV1Outcome status) {
    return status == UNICODE_V1_OK || fail(source, at, "re:allocation-failure", "class interpretation storage");
}
static bool scalar(Atom *tree, uint32_t *out) {
    if (label(tree)) tree = child(tree, 0u);
    if (!tree || tree->kind != ATOM_EXPR || tree->expr.len != 2u ||
        !atom_is_symbol(tree->expr.elems[0], "cp")) return false;
    Atom *value = tree->expr.elems[1];
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT || value->ground.ival < 0 ||
        value->ground.ival > 0x10ffff || (value->ground.ival >= 0xd800 && value->ground.ival <= 0xdfff)) return false;
    *out = (uint32_t)value->ground.ival; return true;
}
static bool fold(ClassSource *source, Atom *at, UnicodeV1Set *set) {
    if (!source->options->insensitive) return true;
    if (source->options->unicode)
        return allocation(source, at, unicode_v1_set_simple_fold(set, set));
    CettaLpNativeUnicodeRange points[52]; size_t count = 0u;
    for (uint32_t cp = 'A'; cp <= 'Z'; ++cp) {
        if (unicode_v1_set_contains(set, cp)) points[count++] = (CettaLpNativeUnicodeRange){cp + 32u, cp + 32u};
        if (unicode_v1_set_contains(set, cp + 32u)) points[count++] = (CettaLpNativeUnicodeRange){cp, cp};
    }
    UnicodeV1Set extra = {0};
    bool ok = allocation(source, at, unicode_v1_set_project(UNICODE_V1_SCALARS, (UnicodeV1Ranges){points, count}, &extra)) &&
        allocation(source, at, unicode_v1_set_combine(UNICODE_V1_UNION, set, &extra, set));
    unicode_v1_set_free(&extra); return ok;
}
static bool primitive(ClassSource *source, Atom *at, UnicodeV1Ranges ranges, bool negative, UnicodeV1Set *out) {
    return allocation(source, at, unicode_v1_set_project(UNICODE_V1_SCALARS, ranges, out)) &&
        fold(source, at, out) && (!negative || allocation(source, at, unicode_v1_set_complement(out, out)));
}
static bool point(ClassSource *source, Atom *at, uint32_t low, uint32_t high, UnicodeV1Set *out) {
    if (low > high) return fail(source, at, "re:source-fault", "reversed character range");
    CettaLpNativeUnicodeRange range = {low, high};
    return primitive(source, at, (UnicodeV1Ranges){&range, 1u}, false, out);
}
static bool escaped_scalar(uint32_t cp, uint32_t *out) {
    switch (cp) {
    case 'n': *out = 10u; return true;
    case 'r': *out = 13u; return true;
    case 't': *out = 9u; return true;
    case 'a': *out = 7u; return true;
    case 'f': *out = 12u; return true;
    case 'v': *out = 11u; return true;
    case 'b': *out = 8u; return true;
    default:
        if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9')) return false;
        *out = cp; return true;
    }
}
static bool hex(ClassSource *source, Atom *tree, uint32_t *out) {
    uint32_t value = 0u; bool nonempty = false;
    while (tree) {
        if (!take(source, tree)) return false;
        const char *name = label(tree); uint32_t cp, digit;
        if (!name || (strcmp(name, "re-src:hex-one") && strcmp(name, "re-src:hex-more")) ||
            !arity(tree, !strcmp(name, "re-src:hex-one") ? 1u : 2u) ||
            !scalar(child(tree, 0u), &cp)) return fail(source, tree, "re:source-fault", "malformed hexadecimal endpoint");
        if (cp >= '0' && cp <= '9') digit = cp - '0';
        else if (cp >= 'A' && cp <= 'F') digit = cp - 'A' + 10u;
        else if (cp >= 'a' && cp <= 'f') digit = cp - 'a' + 10u;
        else return fail(source, tree, "re:source-fault", "expected ASCII hexadecimal digit");
        if (value > (0x10ffffu - digit) / 16u) return fail(source, tree, "re:source-fault", "hexadecimal value outside scalar carrier");
        value = value * 16u + digit; nonempty = true;
        tree = child(tree, 1u);
    }
    if (!nonempty || (value >= 0xd800u && value <= 0xdfffu))
        return fail(source, tree, "re:source-fault", "hexadecimal value is not a scalar");
    *out = value; return true;
}
static bool endpoint(ClassSource *source, Atom *tree, uint32_t *out) {
    const char *name = label(tree);
    if (!name) return fail(source, tree, "re:source-fault", "missing range endpoint");
    if (!strcmp(name, "re-src:endpoint-amp") || !strcmp(name, "re-src:first-endpoint-amp") ||
        !strcmp(name, "re-src:endpoint-tilde") || !strcmp(name, "re-src:first-endpoint-tilde") ||
        !strcmp(name, "re-src:endpoint-open") || !strcmp(name, "re-src:first-endpoint-open")) {
        if (!arity(tree, 0u)) return fail(source, tree, "re:source-fault", "malformed punctuation endpoint");
        *out = strstr(name, "-amp") ? '&' : strstr(name, "-tilde") ? '~' : '['; return true;
    }
    if (!arity(tree, 1u)) return fail(source, tree, "re:source-fault", "malformed range endpoint arity");
    if (!strcmp(name, "re-src:endpoint-char") || !strcmp(name, "re-src:first-endpoint-char"))
        return scalar(child(tree, 0u), out) || fail(source, tree, "re:source-fault", "malformed range endpoint");
    if (strstr(name, "endpoint-hex") || strstr(name, "endpoint-unicode-hex")) return hex(source, child(tree, 0u), out);
    if (strstr(name, "endpoint-escape")) {
        uint32_t cp;
        return (scalar(child(tree, 0u), &cp) && escaped_scalar(cp, out)) ||
            fail(source, tree, "re:source-fault", "range endpoint must denote one scalar");
    }
    /* Earlier selected CSTs retain their direct scalar endpoint carrier. */
    return scalar(tree, out) || fail(source, tree, "re:source-fault", "range endpoint must denote one scalar");
}
static bool property(ClassSource *source, Atom *at, Atom *tree, bool negative, UnicodeV1Set *out) {
    char *name = NULL; size_t len = 0u, capacity = 0u;
    while (tree) {
        if (!take(source, tree)) { free(name); return false; }
        const char *part = label(tree); uint32_t cp;
        if (!part || (strcmp(part, "re-src:property-one") && strcmp(part, "re-src:property-more")) ||
            !arity(tree, !strcmp(part, "re-src:property-one") ? 1u : 2u) ||
            !scalar(child(tree, 0u), &cp) || cp == 0u) { free(name); return fail(source, at, "re:source-fault", "malformed property name"); }
        if (len + 5u > capacity) {
            size_t next = capacity ? capacity * 2u : 32u;
            if (next < capacity) { free(name); return fail(source, at, "re:allocation-failure", "property name capacity"); }
            void *p = realloc(name, next);
            if (!p) { free(name); return fail(source, at, "re:allocation-failure", "property name storage"); }
            name = p; capacity = next;
        }
        if (cp < 0x80u) name[len++] = (char)cp;
        else if (cp < 0x800u) { name[len++] = (char)(0xc0u | cp >> 6u); name[len++] = (char)(0x80u | (cp & 63u)); }
        else if (cp < 0x10000u) {
            name[len++] = (char)(0xe0u | cp >> 12u); name[len++] = (char)(0x80u | (cp >> 6u & 63u)); name[len++] = (char)(0x80u | (cp & 63u));
        } else {
            name[len++] = (char)(0xf0u | cp >> 18u); name[len++] = (char)(0x80u | (cp >> 12u & 63u));
            name[len++] = (char)(0x80u | (cp >> 6u & 63u)); name[len++] = (char)(0x80u | (cp & 63u));
        }
        tree = child(tree, 1u);
    }
    if (!len) { free(name); return fail(source, at, "re:source-fault", "empty property name"); }
    name[len] = 0; char *separator = strpbrk(name, "=:"); UnicodeV1Ranges ranges;
    bool found;
    if (separator) { *separator++ = 0; found = unicode_v1_property(name, separator, &ranges); }
    else found = unicode_v1_character_class(name, &ranges);
    free(name);
    return found ? primitive(source, at, ranges, negative, out) :
        fail(source, at, "re:interpretation-needed", "Unicode property is not supplied by the selected provider");
}
static bool escaped(ClassSource *source, Atom *tree, UnicodeV1Set *out) {
    uint32_t cp;
    if (!scalar(child(tree, 0u), &cp)) return fail(source, tree, "re:source-fault", "malformed class escape");
    if (cp == 'd' || cp == 'D' || cp == 's' || cp == 'S' || cp == 'w' || cp == 'W') {
        uint32_t kind = cp >= 'A' && cp <= 'Z' ? cp + 32u : cp;
        bool negative = cp == 'D' || cp == 'S' || cp == 'W'; UnicodeV1Ranges ranges;
        static const CettaLpNativeUnicodeRange digits[] = {{48,57}}, spaces[] = {{9,13},{32,32}},
            word[] = {{48,57},{65,90},{95,95},{97,122}};
        if (!source->options->unicode) ranges = kind == 'd' ? (UnicodeV1Ranges){digits,1u} :
            kind == 's' ? (UnicodeV1Ranges){spaces,2u} : (UnicodeV1Ranges){word,4u};
        else if (!unicode_v1_lexical_class(kind == 'd' ? "UnicodeDecimal" : kind == 's' ? "RegexWhitespace" : "RegexWord", &ranges))
            return fail(source, tree, "re:interpretation-needed", "Unicode shorthand provider is unresolved");
        return primitive(source, tree, ranges, negative, out);
    }
    uint32_t value;
    return escaped_scalar(cp, &value) ? point(source, tree, value, value, out) :
        fail(source, tree, "re:interpretation-needed", "escape needs its declared class interpretation");
}

typedef enum { CLASS_LEAF, CLASS_IDENTITY, CLASS_COMPLEMENT, CLASS_UNION, CLASS_INTERSECTION,
    CLASS_DIFFERENCE, CLASS_SYMMETRIC, CLASS_PREFIX_DASH, CLASS_PREFIX_AMP, CLASS_PREFIX_TILDE } Kind;
static bool classify(Atom *tree, Kind *kind, size_t *children) {
    const char *name = label(tree);
    if (!name) return false;
    *children = 0u; *kind = CLASS_LEAF;
    if (!strcmp(name, "re-src:class") || !strcmp(name, "re-src:class-union") ||
        !strcmp(name, "re-src:first-nested") || !strcmp(name, "re-src:class-nested")) { *kind = CLASS_IDENTITY; *children = 1u; }
    else if (!strcmp(name, "re-src:class-not") || !strcmp(name, "re-src:first-nested-not") || !strcmp(name, "re-src:class-nested-not")) { *kind = CLASS_COMPLEMENT; *children = 1u; }
    else if (!strcmp(name, "re-src:class-first") || !strcmp(name, "re-src:class-tail") ||
        !strcmp(name, "re-src:class-amp-item") || !strcmp(name, "re-src:class-tilde-item")) { *kind = CLASS_UNION; *children = 2u; }
    else if (!strcmp(name, "re-src:class-intersection")) { *kind = CLASS_INTERSECTION; *children = 2u; }
    else if (!strcmp(name, "re-src:class-difference")) { *kind = CLASS_DIFFERENCE; *children = 2u; }
    else if (!strcmp(name, "re-src:class-symmetric-difference")) { *kind = CLASS_SYMMETRIC; *children = 2u; }
    else if (!strcmp(name, "re-src:class-initial-dash")) { *kind = CLASS_PREFIX_DASH; *children = 1u; }
    else if (!strcmp(name, "re-src:class-first-amp") || !strcmp(name, "re-src:class-tail-amp") ||
        !strcmp(name, "re-src:class-tilde-amp")) { *kind = CLASS_PREFIX_AMP; *children = 1u; }
    else if (!strcmp(name, "re-src:class-first-tilde") || !strcmp(name, "re-src:class-tail-tilde") ||
        !strcmp(name, "re-src:class-amp-tilde")) { *kind = CLASS_PREFIX_TILDE; *children = 1u; }
    if (*children && !arity(tree, *children)) return false;
    for (size_t i = 0u; i < *children; ++i) if (!child(tree, i)) return false;
    return true;
}
static bool leaf(ClassSource *source, Atom *tree, UnicodeV1Set *out) {
    const char *name = label(tree); uint32_t low, high;
    out->domain = UNICODE_V1_SCALARS;
    if (!strcmp(name, "re-src:class-empty") || !strcmp(name, "re-src:class-tail-empty") ||
        !strcmp(name, "re-src:class-amp-empty") || !strcmp(name, "re-src:class-tilde-empty")) return arity(tree, 0u);
    if (!strcmp(name, "re-src:class-tail-dash") || !strcmp(name, "re-src:class-amp-dash") || !strcmp(name, "re-src:class-tilde-dash"))
        return arity(tree, 0u) && point(source, tree, '-', '-', out);
    if (!strcmp(name, "re-src:first-open")) return arity(tree, 0u) && point(source, tree, '[', '[', out);
    if (!strcmp(name, "re-src:first-char") || !strcmp(name, "re-src:class-char"))
        return arity(tree, 1u) && scalar(child(tree, 0u), &low) ? point(source, tree, low, low, out) : fail(source, tree, "re:source-fault", "malformed class scalar");
    if (!strcmp(name, "re-src:first-range") || !strcmp(name, "re-src:class-range"))
        return arity(tree, 2u) && endpoint(source, child(tree, 0u), &low) && endpoint(source, child(tree, 1u), &high) && point(source, tree, low, high, out);
    if (!strcmp(name, "re-src:first-escape") || !strcmp(name, "re-src:class-escape")) return arity(tree, 1u) && escaped(source, tree, out);
    if (!strcmp(name, "re-src:first-property") || !strcmp(name, "re-src:class-property") ||
        !strcmp(name, "re-src:first-not-property") || !strcmp(name, "re-src:class-not-property"))
        return arity(tree, 1u) && property(source, tree, child(tree, 0u), strstr(name, "not-property") != NULL, out);
    if (!strcmp(name, "re-src:first-hex") || !strcmp(name, "re-src:class-hex") ||
        !strcmp(name, "re-src:first-unicode-hex") || !strcmp(name, "re-src:class-unicode-hex"))
        return arity(tree, 1u) && hex(source, child(tree, 0u), &low) && point(source, tree, low, low, out);
    return fail(source, tree, "re:source-fault", "unknown character-class syntax");
}
typedef struct { Atom *tree; Kind kind; size_t children, phase, depth; UnicodeV1Set left; } Frame;
static void *grow(void *data, size_t *capacity, size_t wanted, size_t width) {
    if (wanted <= *capacity) return data;
    size_t next = *capacity ? *capacity : 32u;
    while (next < wanted) { if (next > SIZE_MAX / 2u) return NULL; next *= 2u; }
    if (next > SIZE_MAX / width) return NULL;
    void *out = realloc(data, next * width); if (out) *capacity = next; return out;
}
Atom *cetta_re_v1_class_source(const CettaReClassSourceOptionsV1 *options, Atom *tree, Atom **problem) {
    if (!options || !options->arena || !problem) return NULL;
    ClassSource source = {options, problem}; Frame *stack = NULL; size_t len = 0u, capacity = 0u;
    UnicodeV1Set result = {0}; Atom *current = tree; size_t depth = 1u;
    for (;;) {
        if (current) {
            if (!take(&source, current) || (options->depth_limit && depth > options->depth_limit)) {
                if (!*problem) fail(&source, current, "re:incomplete", "class projection depth bound");
                goto failure;
            }
            Kind kind; size_t children;
            if (!classify(current, &kind, &children)) { fail(&source, current, "re:source-fault", "malformed character-class CST"); goto failure; }
            if (children) {
                void *p = grow(stack, &capacity, len + 1u, sizeof(*stack));
                if (!p) { fail(&source, current, "re:allocation-failure", "class projection stack"); goto failure; }
                stack = p; stack[len++] = (Frame){current, kind, children, 0u, depth, {0}};
                current = child(current, 0u); ++depth; continue;
            }
            if (!leaf(&source, current, &result)) goto failure;
            current = NULL;
        }
        if (!len) break;
        Frame *frame = &stack[len - 1u];
        if (frame->children == 2u && frame->phase == 0u) {
            frame->left = result; memset(&result, 0, sizeof(result)); frame->phase = 1u;
            current = child(frame->tree, 1u); depth = frame->depth + 1u; continue;
        }
        if (frame->children == 2u) {
            UnicodeV1Operation operation = frame->kind == CLASS_UNION ? UNICODE_V1_UNION :
                frame->kind == CLASS_INTERSECTION ? UNICODE_V1_INTERSECTION :
                frame->kind == CLASS_DIFFERENCE ? UNICODE_V1_DIFFERENCE : UNICODE_V1_SYMMETRIC_DIFFERENCE;
            if (!allocation(&source, frame->tree, unicode_v1_set_combine(operation, &frame->left, &result, &result))) goto failure;
            unicode_v1_set_free(&frame->left);
        } else if (frame->kind == CLASS_COMPLEMENT) {
            if (!allocation(&source, frame->tree, unicode_v1_set_complement(&result, &result))) goto failure;
        } else if (frame->kind == CLASS_PREFIX_DASH || frame->kind == CLASS_PREFIX_AMP || frame->kind == CLASS_PREFIX_TILDE) {
            uint32_t cp = frame->kind == CLASS_PREFIX_DASH ? '-' : frame->kind == CLASS_PREFIX_AMP ? '&' : '~';
            UnicodeV1Set prefix = {0};
            bool ok = point(&source, frame->tree, cp, cp, &prefix) &&
                allocation(&source, frame->tree, unicode_v1_set_combine(UNICODE_V1_UNION, &prefix, &result, &result));
            unicode_v1_set_free(&prefix); if (!ok) goto failure;
        }
        --len;
    }
    Atom *ranges = atom_symbol(options->arena, "LNil");
    for (size_t i = result.len; i; --i) {
        CettaLpNativeUnicodeRange range = result.items[i - 1u];
        ranges = atom_expr3(options->arena, atom_symbol(options->arena, "LCons"),
            atom_expr3(options->arena, atom_symbol(options->arena, "re:range"), atom_int(options->arena, range.low), atom_int(options->arena, range.high)), ranges);
    }
    unicode_v1_set_free(&result); free(stack);
    return atom_expr2(options->arena, atom_symbol(options->arena, "re:class"), ranges);
failure:
    unicode_v1_set_free(&result);
    for (size_t i = 0u; i < len; ++i) unicode_v1_set_free(&stack[i].left);
    free(stack);
    if (!*problem) fail(&source, tree, "re:source-fault", "malformed character-class leaf");
    return NULL;
}
