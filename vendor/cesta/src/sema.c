/* The C semantic layer's context and commands: `cesta bind` exports name
   binding, and `cesta types` exports elaborated C types. */

#define _POSIX_C_SOURCE 200809L
#include "sema.h"
#include "pp.h"

#include <stdlib.h>
#include <string.h>

static const char *kind_names[] = {
    "",
#define CE_SEMA_NAME(name) #name,
    CE_SEMA_LABELS(CE_SEMA_NAME)
#undef CE_SEMA_NAME
};

bool ce_sema_init(CeSema *sema, const CeGrammar *g, CeText source, CeError *error) {
    *sema = (CeSema){.grammar=g,.source=source,.abi=&ce_abi_x86_64_sysv};
    sema->kinds = calloc(g->nproductions ? g->nproductions : 1,sizeof(*sema->kinds));
    if (!sema->kinds) return ce_error(error,0,"out of memory");
    for (size_t p = 0; p < g->nproductions; ++p)
        for (unsigned k = 1; k < CE_K_COUNT; ++k)
            if (ce_is(g->productions[p].label,kind_names[k])) { sema->kinds[p] = k; break; }
    return true;
}
void ce_sema_free(CeSema *sema) {
    free(sema->kinds); free(sema->entities); free(sema->uses);
    ce_pointer_map_free(&sema->use_of); ce_pointer_map_free(&sema->entity_of);
    ce_pointer_map_free(&sema->tag_of); ce_pointer_map_free(&sema->exprs);
    ce_pointer_map_free(&sema->type_names); ce_pointer_map_free(&sema->extents);
    ce_arena_free(&sema->arena);
}

static const char *entity_kind_name(unsigned kind) {
    static const char *names[] = {"object","function","typedef","enumerator","parameter",
        "struct","union","enum","label","builtin"};
    return kind < sizeof(names)/sizeof(*names) ? names[kind] : "unknown";
}
static const char *role_name(unsigned role) {
    static const char *names[] = {"reference","declaration","typedef-name","tag","label","attribute"};
    return role < sizeof(names)/sizeof(*names) ? names[role] : "unknown";
}
static void write_name(FILE *file, CeText name) {
    if (!name.len) fputs("()",file);
    else ce_quote(file,name);
}

/* When Cesta preprocessed the input, each printed offset also gets the
   position it was spelled at and the expansions that produced it. */
static const CePPResult *located;
static CeText named(const char *s) { return (CeText){(unsigned char *)s,strlen(s)}; }
CeText ce_builtin_CPPGNUGrammar(void);

static bool json_utf8(CeText text) {
    for (size_t i = 0; i < text.len;) {
        unsigned c = text.data[i++], n, minimum;
        if (c < 128) continue;
        if (c >= 194 && c <= 223) { n = 1; minimum = 128; c &= 31; }
        else if (c >= 224 && c <= 239) { n = 2; minimum = 2048; c &= 15; }
        else if (c >= 240 && c <= 244) { n = 3; minimum = 65536; c &= 7; }
        else return false;
        while (n--) {
            if (i >= text.len || (text.data[i] & 192) != 128) return false;
            c = (c << 6) | (text.data[i++] & 63);
        }
        if (c < minimum || c > 1114111 || (c >= 55296 && c <= 57343)) return false;
    }
    return true;
}
static void json_text(FILE *file, CeText text) {
    fputc('"',file);
    for (size_t i = 0; i < text.len; ++i) {
        unsigned c = text.data[i];
        if (c == '"' || c == '\\') { fputc('\\',file); fputc((int)c,file); }
        else if (c < 32) fprintf(file,"\\u%04x",c);
        else fputc((int)c,file);
    }
    fputc('"',file);
}
static void json_atom(FILE *file, const CeAtom *atom) {
    if (atom->kind == CE_ATOM_LIST) {
        fputs("{\"kind\":\"list\",\"items\":[",file);
        for (size_t i = 0; i < atom->len; ++i) {
            if (i) fputc(',',file);
            json_atom(file,atom->items[i]);
        }
        fputs("]}",file);
    } else {
        fprintf(file,"{\"kind\":\"%s\",",atom->kind == CE_ATOM_SYMBOL ? "symbol" : "text");
        if (json_utf8(atom->text)) { fputs("\"value\":",file); json_text(file,atom->text); }
        else {
            fputs("\"bytes\":\"",file);
            for (size_t i = 0; i < atom->text.len; ++i) fprintf(file,"%02x",atom->text.data[i]);
            fputc('"',file);
        }
        fputc('}',file);
    }
}
static bool json_records(CeArena *arena, CeText data, const char *command, CeError *error) {
    CeAtom *root;
    if (!ce_atom_read(arena,data,&root,error) || root->kind != CE_ATOM_LIST || !root->len)
        return ce_error(error,0,"analysis did not produce a record sequence");
    for (size_t i = 1; i < root->len; ++i) {
        fputs("{\"command\":",stdout); json_text(stdout,named(command));
        fputs(",\"record\":",stdout); json_atom(stdout,root->items[i]); fputs("}\n",stdout);
    }
    return !ferror(stdout);
}

static void locate(FILE *file, size_t offset) {
    if (!located) return;
    const CePPOrigin *o = ce_pp_origin(located,offset);
    if (!o) return;
    fputs(" (at ",file); ce_quote(file,named(o->file)); fprintf(file," %u %u",o->line,o->column);
    for (const CePPExpansion *e = o->expansion; e; e = e->parent) {
        fputs(" (from ",file); ce_quote(file,e->macro); fputc(' ',file);
        ce_quote(file,named(e->definition_file)); fprintf(file," %u ",e->definition_line);
        ce_quote(file,named(e->file)); fprintf(file," %u %u)",e->line,e->column);
    }
    fputc(')',file);
}

/* (entity ID KIND NAME LINKAGE SCOPE DECLARATION-OFFSET) and
   (use OFFSET ENTITY ROLE VISIBLE-OFFSET), offsets into the parsed source. */
bool ce_sema_bindings_export(const CeSema *sema, FILE *file) {
    static const char *linkage[] = {"none","internal","external"};
    static const char *scope[] = {"file","block","prototype","function"};
    fputs("(bindings\n",file);
    for (size_t i = 0; i < sema->nentities; ++i) {
        const CeEntity *e = sema->entities[i];
        fprintf(file," (entity %zu %s ",e->id,entity_kind_name(e->kind));
        write_name(file,e->name);
        fprintf(file," %s %s ",linkage[e->linkage],scope[e->scope]);
        if (e->declaration) { fprintf(file,"%zu",e->declaration->offset); locate(file,e->declaration->offset); fputs(")\n",file); }
        else fputs("())\n",file);
    }
    for (size_t i = 0; i < sema->nuses; ++i) {
        const CeUse *u = &sema->uses[i];
        fprintf(file," (use %zu %zu %s ",u->node->offset,u->entity->id,role_name(u->role));
        if (u->visible) fprintf(file,"%zu",u->visible->offset); else fputs("()",file);
        if (u->folded) fputs(" folded",file);
        locate(file,u->node->offset);
        fputs(")\n",file);
    }
    fputs(")\n",file);
    return !ferror(file);
}

/* ---- source extents --------------------------------------------------------

   A node's byte extent is recomputed from the token stream and the grammar:
   each production consumes its literal tokens and its children in order, and
   the only transparent production with tokens, a parenthesized expression,
   adds matching "(" ")" around a child. The root must consume every token. */

typedef struct { size_t begin, end; } Extent;
typedef struct {
    CeSema *sema;
    const CeTokens *tokens;
    CeError *error;
    size_t *separators, *terminators;   /* list symbol -> literal tokens between / after elements */
} Spanner;

static size_t token_at(const CeTokens *tokens, size_t offset) {
    size_t low = 0, high = tokens->len;
    while (low < high) {
        size_t mid = (low+high)/2;
        if (tokens->data[mid].offset < offset) low = mid+1; else high = mid;
    }
    return low;
}
/* Literal tokens in the stream: real spellings, and virtual symbols that a
   lexical pair rewrites onto a real token (`_Atomic (`); the parser's name
   markers are not in the stream. */
static bool consumes(const CeGrammar *g, int symbol) {
    if (g->symbols[symbol].kind != CE_LITERAL || g->symbols[symbol].erased) return false;
    if (!g->symbols[symbol].virtual_token) return true;
    for (size_t i = 0; i < g->npairs; ++i) if (g->pairs[i].target == symbol) return true;
    return false;
}
static bool span(Spanner *s, const CeTree *t, size_t at, size_t *end);
static bool span_child(Spanner *s, const CeTree *child, size_t at, size_t *end) {
    const CeTokens *tokens = s->tokens;
    size_t first = token_at(tokens,child->offset);
    if (first < at) first = at;
    size_t k = first-at;
    for (size_t i = 0; i < k; ++i)
        if (at+i >= tokens->len || !ce_is(tokens->data[at+i].text,"("))
            return ce_error(s->error,child->offset,"source extent: unexpected token %.*s before a child %s (at %zu, first %zu)",(int)tokens->data[at+i].text.len,tokens->data[at+i].text.data,child->production >= 0 ? (const char *)s->sema->grammar->productions[child->production].label.data : "list/token",at,first);
    size_t inner;
    if (!span(s,child,first,&inner)) return false;
    for (size_t i = 0; i < k; ++i)
        if (inner+i >= tokens->len || !ce_is(tokens->data[inner+i].text,")"))
            return ce_error(s->error,child->offset,"source extent: unbalanced parentheses around a child");
    *end = inner+k;
    return true;
}
static bool span(Spanner *s, const CeTree *t, size_t at, size_t *end) {
    const CeGrammar *g = s->sema->grammar;
    size_t position = at;
    if (t->production == -1) position = at+1;
    else if (t->production == -2) {
        for (size_t i = 0; i < t->len; ++i) {
            if (!span_child(s,t->children[i],position,&position)) return false;
            position += i+1 < t->len ? s->separators[t->symbol] : s->terminators[t->symbol];
        }
    } else {
        const CeProduction *p = &g->productions[t->production];
        size_t child = 0;
        for (size_t i = 0; i < p->len; ++i) {
            int symbol = p->rhs[i];
            if (g->symbols[symbol].erased) continue;
            if (g->symbols[symbol].kind == CE_LITERAL) { if (consumes(g,symbol)) ++position; continue; }
            if (child >= t->len) return ce_error(s->error,t->offset,"source extent: missing child");
            if (!span_child(s,t->children[child++],position,&position)) return false;
        }
    }
    if (position > s->tokens->len) return ce_error(s->error,t->offset,"source extent runs past the input");
    Extent *x = ce_alloc(&s->sema->arena,sizeof(*x));
    x->begin = position > at ? s->tokens->data[at].offset : t->offset;
    x->end = position > at ? s->tokens->data[position-1].offset + s->tokens->data[position-1].text.len : t->offset;
    ce_pointer_put(&s->sema->extents,t,x);
    *end = position;
    return true;
}
bool ce_sema_extents(CeSema *sema, const CeTree *root, const CeTokens *tokens, CeError *error) {
    const CeGrammar *g = sema->grammar;
    Spanner s = {sema,tokens,error,calloc(g->nsymbols,sizeof(size_t)),calloc(g->nsymbols,sizeof(size_t))};
    /* A list's (:) production holds the literals between elements; its (:[])
       production, when present, those after the last (terminator lists
       without one repeat the separator). */
    bool *has_last = calloc(g->nsymbols,sizeof(bool));
    for (size_t p = 0; p < g->nproductions; ++p) {
        const CeProduction *rule = &g->productions[p];
        if (g->symbols[rule->lhs].list_element < 0) continue;
        size_t literals = 0;
        for (size_t i = 0; i < rule->len; ++i) if (consumes(g,rule->rhs[i])) ++literals;
        if (ce_is(rule->label,"(:)")) s.separators[rule->lhs] = literals;
        else if (ce_is(rule->label,"(:[])")) { s.terminators[rule->lhs] = literals; has_last[rule->lhs] = true; }
    }
    for (size_t i = 0; i < g->nsymbols; ++i)
        if (g->symbols[i].list_element >= 0 && !has_last[i]) s.terminators[i] = s.separators[i];
    free(has_last);
    size_t end;
    bool ok = span(&s,root,0,&end);
    if (ok && end != tokens->len) ok = ce_error(error,0,"source extents cover %zu of %zu tokens",end,tokens->len);
    free(s.separators); free(s.terminators);
    return ok;
}
static const Extent *extent_of(const CeSema *sema, const CeTree *t) { return ce_pointer_get(&sema->extents,t); }

/* ---- exports ---------------------------------------------------------------- */

static const char *category_name(unsigned c) {
    return c == CE_LVALUE ? "lvalue" : c == CE_DESIGNATOR ? "designator" : "rvalue";
}
static void write_type(CeSema *sema, FILE *file, const CeType *t) {
    char *text = ce_type_text(sema,t,CE_PRINT_ANYWAY);
    CeText quoted = {(unsigned char *)text,strlen(text)};
    ce_quote(file,quoted); free(text);
}
/* (expr BEGIN END "TYPE" CATEGORY (KIND "TO")... [bits N] [value V]) in tree order. */
static void types_walk(CeSema *sema, const CeTree *t, FILE *file) {
    const CeExpr *x = ce_sema_expr(sema,t);
    const Extent *span = extent_of(sema,t);
    if (x && span) {
        fprintf(file," (expr %zu %zu ",span->begin,span->end);
        write_type(sema,file,x->type);
        fprintf(file," %s",category_name(x->category));
        if (x->cast) fprintf(file," (cast %s)",ce_conversion_names[x->cast]);
        for (unsigned i = 0; i < x->nconversions; ++i) {
            fprintf(file," (%s ",ce_conversion_names[x->conversions[i].kind]);
            write_type(sema,file,x->conversions[i].to); fputc(')',file);
        }
        if (x->bits) fprintf(file," (bits %u)",x->bits);
        if (x->constant) fprintf(file," (value %llu)",x->value);
        if (x->folded) fputs(" (folded)",file);
        locate(file,span->begin);
        fputs(")\n",file);
    }
    for (size_t i = 0; i < t->len; ++i) types_walk(sema,t->children[i],file);
}
static bool types_export(CeSema *sema, const CeTree *root, FILE *file) {
    fputs("(types\n",file);
    for (size_t i = 0; i < sema->nentities; ++i) {
        const CeEntity *e = sema->entities[i];
        if (!e->type || e->kind == CE_ENTITY_STRUCT || e->kind == CE_ENTITY_UNION || e->kind == CE_ENTITY_ENUM) continue;
        fprintf(file," (entity %zu ",e->id); write_type(sema,file,e->type); fputs(")\n",file);
    }
    for (size_t i = 0; i < sema->nentities; ++i) {
        const CeEntity *e = sema->entities[i];
        if (!e->record || !e->record->complete || e->kind == CE_ENTITY_ENUM) continue;
        /* (record ID KIND TAG LINE:COLUMN SIZE ALIGN (MEMBER OFFSET [BIT WIDTH])...) */
        size_t line = 1, column = 1, at = e->definition ? e->definition->offset : 0;
        for (size_t k = 0; k < at && k < sema->source.len; ++k) {
            if (sema->source.data[k] == '\n') { ++line; column = 1; } else ++column;
        }
        fprintf(file," (record %zu %s ",e->id,e->kind == CE_ENTITY_UNION ? "union" : "struct");
        write_name(file,e->name);
        /* natural: laid out by alignment alone, with no bit-field, attribute,
           packing, flexible array member or zero-size member */
        bool natural = !e->record->packed && !e->record->attributed && !e->record->flexible && e->record->nmembers;
        for (size_t m = 0; m < e->record->nmembers; ++m) {
            const CeType *t = e->record->members[m].type;
            unsigned long long size = ce_type_size(sema,t);
            if (e->record->members[m].bitfield || !size || size % ce_type_align(sema,t)) natural = false;
        }
        fprintf(file," %zu:%zu %llu %u %s",e->definition ? line : 0,e->definition ? column : 0,e->record->size,e->record->align,
                natural ? "natural" : "special");
        for (size_t m = 0; m < e->record->nmembers; ++m) {
            const CeMember *mb = &e->record->members[m];
            fputs(" (",file);
            if (mb->name.len) fwrite(mb->name.data,1,mb->name.len,file); else fputs("_",file);
            fprintf(file," %llu %llu %u",mb->offset,ce_type_size(sema,mb->type),ce_type_align(sema,mb->type));
            if (mb->bitfield) fprintf(file," %u %u",mb->bit_offset,mb->bit_width);
            fputc(')',file);
        }
        fputs(")\n",file);
    }
    types_walk(sema,root,file);
    for (unsigned a = 0; a < 128; ++a) for (unsigned b = 0; b < 128; ++b)
        if (sema->arithmetic_used[a][b]) fprintf(file," (arithmetic %u %u)\n",a,b);
    fputs(")\n",file);
    return !ferror(file);
}

/* cesta abi-tables: the target's integer promotions and usual arithmetic
   conversions over every arithmetic type, as (promote K P) and (usual A B C)
   with A, B, C arithmetic codes: integer and real kinds, complex = 64 + real. */
static const unsigned arithmetic_kinds[] = {
    CE_T_BOOL, CE_T_CHAR, CE_T_SCHAR, CE_T_UCHAR, CE_T_SHORT, CE_T_USHORT, CE_T_INT, CE_T_UINT,
    CE_T_LONG, CE_T_ULONG, CE_T_LLONG, CE_T_ULLONG, CE_T_INT128, CE_T_UINT128, CE_T_FLOAT,
    CE_T_DOUBLE, CE_T_LDOUBLE, CE_T_FLOAT32, CE_T_FLOAT64, CE_T_FLOAT128, CE_T_FLOAT32X, CE_T_FLOAT64X,
    CE_T_FLOAT16, CE_T_BFLOAT16
};
static CeType *arithmetic_type(CeSema *sema, unsigned code) {
    if (code < 64) return ce_type_basic(sema,code);
    CeType *c = ce_type_new(sema,CE_T_COMPLEX); c->base = ce_type_basic(sema,code-64); return c;
}
static unsigned code_of(const CeType *t) { return t->kind == CE_T_COMPLEX ? 64+t->base->kind : t->kind; }
int ce_abi_tables_command(const CeAbi *abi) {
    CeSema sema = {.abi=abi};
    unsigned codes[64]; size_t n = 0;
    size_t nk = sizeof(arithmetic_kinds)/sizeof(*arithmetic_kinds);
    /* the types this target has */
    for (size_t i = 0; i < nk; ++i) if (abi->size[arithmetic_kinds[i]]) codes[n++] = arithmetic_kinds[i];
    /* GCC and Clang have no complex __bf16 */
    for (size_t i = 0; i < nk; ++i) if (arithmetic_kinds[i] >= CE_T_FLOAT && arithmetic_kinds[i] != CE_T_BFLOAT16 &&
                                        abi->size[arithmetic_kinds[i]]) codes[n++] = 64+arithmetic_kinds[i];
    printf("(abi-tables %s\n",sema.abi->name);
    for (size_t i = 0; i < nk; ++i) if (arithmetic_kinds[i] <= CE_T_UINT128 && abi->size[arithmetic_kinds[i]])
        printf(" (promote %u %u)\n",arithmetic_kinds[i],ce_promoted_kind(sema.abi,arithmetic_kinds[i],0));
    for (size_t i = 0; i < n; ++i) for (size_t j = 0; j < n; ++j) {
        CeType *c = ce_type_common(&sema,arithmetic_type(&sema,codes[i]),arithmetic_type(&sema,codes[j]),0,0);
        printf(" (usual %u %u %u)\n",codes[i],codes[j],code_of(c));
    }
    puts(")");
    ce_arena_free(&sema.arena);
    return 0;
}

/* ---- compiler probes ---------------------------------------------------------

   Every typed expression E is rewritten as
     _Generic((struct { char __cesta_probe_B_E[__builtin_types_compatible_p(__typeof__(E) *, T *) ? 1 : -1]; } *)0,
              default: (E'))
   (an rvalue compares __typeof__(E) with T directly)
   where T is its elaborated type and E' its rewritten operands. A generic
   selection keeps E's value, type and category, and the controlling cast is
   never evaluated, so the program means what it meant; a wrong T makes the
   array size negative and the compiler refuses the unit, naming the probe's
   source extent B..E. */

typedef struct {
    CeSema *sema;
    FILE *file;
    size_t probes, skipped;
    size_t unnamed, bitfields, statement_expressions;
    size_t constants;
} Prober;

/* Expressions a probe cannot repeat: statement expressions (their labels and
   declarations would be duplicated) and va_start, which must name the
   enclosing function's last parameter from inside that function. */
static bool contains_statement_expression(const CeSema *sema, const CeTree *t) {
    if (ce_sema_kind(sema,t) == CE_K_GNUStatementExpression) return true;
    if (ce_sema_kind(sema,t) == CE_K_Call && ce_sema_kind(sema,t->children[0]) == CE_K_Var) {
        const CeUse *use = ce_sema_use(sema,t->children[0]->children[0]);
        if (use && use->entity->kind == CE_ENTITY_BUILTIN && ce_is(use->entity->name,"__builtin_va_start")) return true;
    }
    for (size_t i = 0; i < t->len; ++i) if (contains_statement_expression(sema,t->children[i])) return true;
    return false;
}
static void emit_range(Prober *p, size_t begin, size_t end) {
    if (end > begin) fwrite(p->sema->source.data+begin,1,end-begin,p->file);
}
static void emit(Prober *p, const CeTree *t);
/* A known short-circuit result can contain nonconstant operands and need not
   be an integer constant expression accepted as an array bound by Clang. */
static bool constant_value_checkable(const CeSema *sema, const CeTree *t) {
    const CeExpr *x = ce_sema_expr(sema,t);
    if (x && !x->constant) return false;
    unsigned kind = ce_sema_kind(sema,t);
    if (kind == CE_K_SizeofExpression || kind == CE_K_SizeofType || kind == CE_K_Alignof ||
        kind == CE_K_GNUAlignofExpression || kind == CE_K_GNUAlignofType) return x && x->constant;
    for (size_t i = 0; i < t->len; ++i)
        if (!constant_value_checkable(sema,t->children[i])) return false;
    return true;
}
static void emit_inner(Prober *p, const CeTree *t, const Extent *span) {
    size_t at = span->begin;
    for (size_t i = 0; i < t->len; ++i) {
        const Extent *c = extent_of(p->sema,t->children[i]);
        if (!c || c->end <= c->begin) continue;
        emit_range(p,at,c->begin);
        emit(p,t->children[i]);
        at = c->end;
    }
    emit_range(p,at,span->end);
}
static void emit(Prober *p, const CeTree *t) {
    const Extent *span = extent_of(p->sema,t);
    if (!span) return;
    const CeExpr *x = ce_sema_expr(p->sema,t);
    char *pointer = NULL;
    bool probe = x && !x->no_probe && !x->builtin;
    if (probe && contains_statement_expression(p->sema,t)) { probe = false; ++p->statement_expressions; }
    /* An lvalue's qualifiers decide what it may do, so it is compared through a
       pointer; an rvalue has no qualifiers (§6.3.2.1p2) and is compared as is,
       since __builtin_types_compatible_p ignores top-level qualifiers. */
    bool through_pointer = x && x->category == CE_LVALUE && !x->bits;
    if (probe) {
        CeType *checked = x->bits ? ce_type_promoted(p->sema,x->type,x->bits) : x->type;
        pointer = ce_type_text(p->sema,through_pointer ? ce_type_pointer(p->sema,checked) : checked,CE_PRINT_COMPATIBLE);
        if (!pointer) { probe = false; ++p->unnamed; }
    }
    if (!probe) { if (x) ++p->skipped; emit_inner(p,t,span); return; }
    ++p->probes;
    if (x->bits) ++p->bitfields;
    /* the member name locates a refused probe in the compiler's message */
    fprintf(p->file,"_Generic((struct { char __cesta_probe_%zu_%zu[__builtin_types_compatible_p(__typeof__(",span->begin,span->end);
    if (x->bits) fputc('+',p->file);
    fputc('(',p->file);
    emit_range(p,span->begin,span->end);
    fprintf(p->file,"))%s, %s)",through_pointer ? " *" : "",pointer);
    if (x->constant && ce_type_integer(x->type) && ce_kind_width(p->sema->abi,ce_type_integer_kind(x->type)) <= 64 &&
        constant_value_checkable(p->sema,t)) {
        fputs(" && ((unsigned long long)(",p->file);
        emit_range(p,span->begin,span->end);
        fprintf(p->file,") == 0x%llxULL)",x->value);
        ++p->constants;
    }
    fputs(" ? 1 : -1]; } *)0, default: (",p->file);
    emit_inner(p,t,span);
    fputs("))",p->file);
    free(pointer);
}
/* After the unit: every complete file-scope record's size, alignment and
   member offsets, and every file-scope enumeration constant's value. */
static void layout_probes(Prober *p, size_t *records, size_t *members, size_t *constants, size_t *unnamed) {
    CeSema *sema = p->sema;
    fputs("\n",p->file);
    for (size_t i = 0; i < sema->nentities; ++i) {
        CeEntity *e = sema->entities[i];
        if (e->scope != CE_SCOPE_FILE || e->builtin_tag) continue;
        if (e->kind == CE_ENTITY_ENUMERATOR && e->value_known && e->type) {
            fprintf(p->file,"_Static_assert(%.*s == %lldLL, \"__cesta_constant_%zu\");\n",
                    (int)e->name.len,e->name.data,e->value,e->id);
            ++*constants; continue;
        }
        if (!(e->kind == CE_ENTITY_STRUCT || e->kind == CE_ENTITY_UNION) || !e->record || !e->record->complete) continue;
        CeType *t = ce_type_new(sema,e->kind == CE_ENTITY_UNION ? CE_T_UNION : CE_T_STRUCT); t->tag = e;
        /* after the unit only file-scope names are visible */
        if (!e->name.len && !e->naming_alias && e->witness_object && e->witness_object->scope != CE_SCOPE_FILE) {
            ++*unnamed; continue;
        }
        char *name = ce_type_text(sema,t,CE_PRINT_COMPATIBLE);
        if (!name) { ++*unnamed; continue; }
        /* an anonymous record written through its typedef has that name's alignment */
        unsigned align = e->record->align;
        if (!e->name.len && e->naming_alias && e->naming_alias->type && e->naming_alias->type->alignment)
            align = e->naming_alias->type->alignment;
        fprintf(p->file,"_Static_assert(sizeof(%s) == %llu && _Alignof(%s) == %u, \"__cesta_layout_%zu\");\n",
                name,e->record->size,name,align,e->id);
        ++*records;
        for (size_t m = 0; m < e->record->nmembers; ++m) {
            const CeMember *mb = &e->record->members[m];
            if (!mb->name.len || mb->bitfield) continue;
            fprintf(p->file,"_Static_assert(__builtin_offsetof(%s, %.*s) == %llu, \"__cesta_offset_%zu_%zu\");\n",
                    name,(int)mb->name.len,mb->name.data,mb->offset,e->id,m);
            ++*members;
        }
        free(name);
    }
}

static bool probe_export(CeSema *sema, const CeTree *root, FILE *file) {
    Prober p = {.sema=sema,.file=file};
    const Extent *span = extent_of(sema,root);
    emit_range(&p,0,span->begin);
    emit(&p,root);
    emit_range(&p,span->end,sema->source.len);
    size_t records = 0, members = 0, constants = 0, unnamed_records = 0;
    layout_probes(&p,&records,&members,&constants,&unnamed_records);
    fprintf(stderr,"layout: %zu records, %zu member offsets, %zu enumeration constants (%zu records unnamed)\n",
            records,members,constants,unnamed_records);
    fprintf(stderr,"probes: %zu typed expressions checked; %zu not rewritten (%zu unnamed types, %zu inside statement expressions); %zu bit-fields by promotion; %zu integer constant values checked\n",
            p.probes,p.skipped,p.unnamed,p.statement_expressions,p.bitfields,p.constants);
    return !ferror(file);
}

/* ---- explicit conversions ----------------------------------------------------

   Every conversion that is not lvalue conversion, decay or an atomic boundary
   becomes a cast to its target: E is written ((T)(E')). Casts and implicit
   conversions mean the same (§6.3, §6.5.4), so the program compiles to the same
   object; a wrongly elaborated conversion changes the code. */

static bool representation_conversion(unsigned kind) {
    switch (kind) {
    case CE_CONVERT_LValueToRValue: case CE_CONVERT_ArrayToPointerDecay: case CE_CONVERT_FunctionToPointerDecay:
    case CE_CONVERT_BuiltinFnToFnPtr: case CE_CONVERT_AtomicToNonAtomic: case CE_CONVERT_NonAtomicToAtomic:
    case CE_CONVERT_ToVoid: case CE_CONVERT_LValueBitCast: return false;
    default: return true;
    }
}
typedef struct { CeSema *sema; FILE *file; size_t casts, unnamed, mutate, integer_casts; } Explicitor;
/* A mutation control: the next narrower integer type of the same signedness,
   a conversion error that changes values. */
static unsigned narrowed(unsigned kind) {
    switch (kind) {
    case CE_T_SHORT: return CE_T_SCHAR;
    case CE_T_USHORT: return CE_T_UCHAR;
    case CE_T_INT: return CE_T_SHORT;
    case CE_T_UINT: return CE_T_USHORT;
    case CE_T_LONG: case CE_T_LLONG: return CE_T_INT;
    case CE_T_ULONG: case CE_T_ULLONG: return CE_T_UINT;
    case CE_T_INT128: return CE_T_LONG;
    case CE_T_UINT128: return CE_T_ULONG;
    default: return kind;
    }
}
static void explicit_emit(Explicitor *x, const CeTree *t);
static void explicit_inner(Explicitor *x, const CeTree *t, const Extent *span) {
    size_t at = span->begin;
    for (size_t i = 0; i < t->len; ++i) {
        const Extent *c = extent_of(x->sema,t->children[i]);
        if (!c || c->end <= c->begin) continue;
        if (c->begin > at) fwrite(x->sema->source.data+at,1,c->begin-at,x->file);
        explicit_emit(x,t->children[i]);
        at = c->end;
    }
    if (span->end > at) fwrite(x->sema->source.data+at,1,span->end-at,x->file);
}
static void explicit_emit(Explicitor *x, const CeTree *t) {
    const Extent *span = extent_of(x->sema,t);
    if (!span) return;
    const CeExpr *e = ce_sema_expr(x->sema,t);
    const CeType *target = NULL;
    for (unsigned i = 0; e && i < e->nconversions; ++i)
        if (representation_conversion(e->conversions[i].kind)) target = e->conversions[i].to;
    /* Atomic initialization belongs to the destination object. Casting to an
       atomic pointer would discard the constant-expression status of a null
       initializer in Clang; emit its underlying representation type. */
    if (target && (target->qualifiers & CE_Q_ATOMIC)) target = ce_type_unqualified(x->sema,target);
    /* narrower than both the operand and the right target, so the cast loses values */
    unsigned narrowest = 0;
    if (target && ce_type_integer(target) && ce_type_integer(e->type)) {
        unsigned a = ce_type_integer_kind(e->type), b = ce_type_integer_kind(target);
        narrowest = narrowed(ce_kind_width(x->sema->abi,a) <= ce_kind_width(x->sema->abi,b) ? a : b);
        if (ce_kind_width(x->sema->abi,narrowest) >= ce_kind_width(x->sema->abi,a) ||
            ce_kind_width(x->sema->abi,narrowest) >= ce_kind_width(x->sema->abi,b)) narrowest = 0;
    }
    if (narrowest && x->mutate && !e->constant && ++x->integer_casts == x->mutate) {
        target = ce_type_basic(x->sema,narrowest);
        fprintf(stderr,"explicit: mutated the conversion at bytes %zu..%zu\n",span->begin,span->end);
    }
    char *name = target ? ce_type_text(x->sema,target,CE_PRINT_COMPATIBLE) : NULL;
    if (target && !name) ++x->unnamed;
    if (!name) { explicit_inner(x,t,span); return; }
    ++x->casts;
    fprintf(x->file,"((%s)(",name);
    explicit_inner(x,t,span);
    fputs("))",x->file);
    free(name);
}
/* ---- the native view: what each implicit conversion does to values ----------- */

enum {
    CLASS_PRESERVING, CLASS_SIGN_CHANGE, CLASS_SIGN_WIDENING, CLASS_NARROWING, CLASS_TO_BOOLEAN,
    CLASS_INTEGER_TO_FLOATING, CLASS_FLOATING_TO_INTEGER, CLASS_FLOATING_NARROWING, CLASS_FLOATING_WIDENING,
    CLASS_POINTER, CLASS_COUNT
};
static const char *class_names[] = {"preserving","sign-change","sign-widening","narrowing","to-boolean",
    "integer-to-floating","floating-to-integer","floating-narrowing","floating-widening","pointer"};

static bool constant_fits(const CeAbi *abi, unsigned long long value, bool negative, unsigned kind) {
    unsigned width = ce_kind_width(abi,kind);
    if (ce_kind_signed(abi,kind)) {
        long long v = (long long)value;
        if (width >= 64) return true;
        long long low = -(1LL << (width-1)), high = (1LL << (width-1))-1;
        return negative ? v >= low : value <= (unsigned long long)high;
    }
    if (negative) return false;
    return width >= 64 || value < (1ULL << width);
}
/* The class of converting a value of type `from` to `to`; a known constant
   that the target represents exactly is preserving. */
static unsigned conversion_class(CeSema *sema, const CeType *from, const CeType *to, const CeExpr *x) {
    const CeAbi *abi = sema->abi;
    if (to->kind == CE_T_POINTER || from->kind == CE_T_POINTER) return to->kind == CE_T_BOOL ? CLASS_TO_BOOLEAN : CLASS_POINTER;
    if (to->kind == CE_T_BOOL) return from->kind == CE_T_BOOL ? CLASS_PRESERVING : CLASS_TO_BOOLEAN;
    bool fi = ce_type_integer(from), ti = ce_type_integer(to);
    if (fi && ti) {
        unsigned fk = ce_type_integer_kind(from), tk = ce_type_integer_kind(to);
        unsigned fw = x->bits ? x->bits : ce_kind_width(abi,fk), tw = ce_kind_width(abi,tk);
        bool fs = ce_kind_signed(abi,fk), ts = ce_kind_signed(abi,tk);
        if (x->constant) return constant_fits(abi,x->value,fs && (long long)x->value < 0,tk) ? CLASS_PRESERVING : CLASS_NARROWING;
        if (fs == ts) return tw >= fw ? CLASS_PRESERVING : CLASS_NARROWING;
        if (!fs && ts) return tw > fw ? CLASS_PRESERVING : tw == fw ? CLASS_SIGN_CHANGE : CLASS_NARROWING;
        return tw == fw ? CLASS_SIGN_CHANGE : tw > fw ? CLASS_SIGN_WIDENING : CLASS_NARROWING;
    }
    if (fi && ce_type_floating(to)) {
        if (x->constant) return CLASS_PRESERVING;
        unsigned fw = ce_kind_width(abi,ce_type_integer_kind(from));
        const CeType *real = to->kind == CE_T_COMPLEX ? to->base : to;
        return fw <= abi->precision[real->kind] ? CLASS_PRESERVING : CLASS_INTEGER_TO_FLOATING;
    }
    if (ce_type_floating(from) && ti) return CLASS_FLOATING_TO_INTEGER;
    if (ce_type_floating(from) && ce_type_floating(to)) {
        const CeType *a = from->kind == CE_T_COMPLEX ? from->base : from, *b = to->kind == CE_T_COMPLEX ? to->base : to;
        if (from->kind == CE_T_COMPLEX && to->kind != CE_T_COMPLEX) return CLASS_FLOATING_NARROWING;
        if (abi->precision[b->kind] > abi->precision[a->kind]) return CLASS_FLOATING_WIDENING;
        return abi->precision[b->kind] == abi->precision[a->kind] ? CLASS_PRESERVING : CLASS_FLOATING_NARROWING;
    }
    return CLASS_PRESERVING;
}
typedef struct { CeSema *sema; FILE *file; size_t counts[2][CLASS_COUNT]; bool list; } Census;
static unsigned bitfield_store_class(CeSema *sema, const CeExpr *value, const CeType *target, unsigned bits) {
    unsigned kind = ce_type_integer_kind(target);
    if (kind == CE_T_BOOL) return CLASS_TO_BOOLEAN;
    unsigned width = ce_kind_width(sema->abi,kind);
    if (bits >= width) return CLASS_PRESERVING;
    if (value->constant && ce_type_integer(value->type)) {
        unsigned long long v = value->value;
        if (width < 64) {
            unsigned long long mask = (1ULL << width)-1;
            v &= mask;
            if (ce_kind_signed(sema->abi,kind) && (v & (1ULL << (width-1)))) v |= ~mask;
        }
        if (ce_kind_signed(sema->abi,kind)) {
            long long bound = 1LL << (bits-1);
            return (long long)v >= -bound && (long long)v < bound ? CLASS_PRESERVING : CLASS_NARROWING;
        }
        return v < (1ULL << bits) ? CLASS_PRESERVING : CLASS_NARROWING;
    }
    return CLASS_NARROWING;
}
static void census_walk(Census *c, const CeTree *t, const Extent *parent) {
    const CeExpr *x = ce_sema_expr(c->sema,t);
    const Extent *span = extent_of(c->sema,t);
    if (x && span) {
        const CeType *from = x->type;
        for (unsigned i = 0; i < x->nconversions; ++i) {
            unsigned kind = x->conversions[i].kind;
            const CeType *to = x->conversions[i].to;
            if (representation_conversion(kind)) {
                unsigned k = conversion_class(c->sema,ce_type_unqualified(c->sema,from),ce_type_unqualified(c->sema,to),x);
                ++c->counts[x->constant][k];
                /* compilers locate an operator's conversion at the operator */
                if (c->list && k != CLASS_PRESERVING && k != CLASS_POINTER) {
                    size_t at = parent ? parent->begin : span->begin;
                    fprintf(c->file," (conversion %zu %zu %s %zu %zu",span->begin,span->end,class_names[k],
                            at,parent ? parent->end : span->end);
                    if (located) {
                        fputs(" (types ",c->file); write_type(c->sema,c->file,from);
                        fputc(' ',c->file); write_type(c->sema,c->file,to); fputc(')',c->file);
                        locate(c->file,span->begin);
                    }
                    fputs(")\n",c->file);
                }
            }
            from = to;
        }
        /* a compound assignment converts its computed value back to the left operand's type */
        if (x->computation && ce_type_arithmetic(x->type)) {
            CeExpr value = {.type=x->computation};
            unsigned k = conversion_class(c->sema,x->computation,x->type,&value);
            ++c->counts[0][k];
            if (c->list && k != CLASS_PRESERVING && k != CLASS_POINTER) {
                fprintf(c->file," (conversion %zu %zu %s %zu %zu",span->begin,span->end,class_names[k],span->begin,span->end);
                if (located) {
                    fputs(" (types ",c->file); write_type(c->sema,c->file,x->computation);
                    fputc(' ',c->file); write_type(c->sema,c->file,x->type); fputc(')',c->file);
                    locate(c->file,span->begin);
                }
                fputs(")\n",c->file);
            }
        }
        unsigned kind = ce_sema_kind(c->sema,t);
        if (x->store_bits || (x->bits && (kind == CE_K_Assign || x->computation))) {
            CeExpr computed = {.type=x->computation};
            const CeExpr *value = x->store_bits ? x : kind == CE_K_Assign ? ce_sema_expr(c->sema,t->children[1]) : &computed;
            unsigned bits = x->store_bits ? x->store_bits : x->bits;
            const CeType *target = x->store_bits ? x->store_type : x->type;
            unsigned k = bitfield_store_class(c->sema,value,target,bits);
            ++c->counts[value->constant][k];
            if (c->list && k != CLASS_PRESERVING) {
                fprintf(c->file," (conversion %zu %zu %s %zu %zu (store-bits %u)",span->begin,span->end,
                        class_names[k],span->begin,span->end,bits);
                if (located) locate(c->file,span->begin);
                fputs(")\n",c->file);
            }
        }
    }
    /* compilers locate a case label's conversion at the label */
    unsigned k = ce_sema_kind(c->sema,t);
    bool locates = (x && span) || ((k == CE_K_Case || k == CE_K_GNUCaseRange) && span);
    for (size_t i = 0; i < t->len; ++i) census_walk(c,t->children[i],locates ? span : parent);
}
static bool census_export(CeSema *sema, const CeTree *root, FILE *file) {
    Census c = {.sema=sema,.file=file,.list=true};
    fputs("(census\n",file);
    census_walk(&c,root,NULL);
    for (unsigned constant = 0; constant < 2; ++constant)
        for (unsigned k = 0; k < CLASS_COUNT; ++k) if (c.counts[constant][k])
            fprintf(file," (count %s %s %zu)\n",constant ? "constant" : "value",class_names[k],c.counts[constant][k]);
    fputs(")\n",file);
    return !ferror(file);
}

static size_t explicit_mutation;
static bool explicit_export(CeSema *sema, const CeTree *root, FILE *file) {
    Explicitor x = {sema,file,0,0,explicit_mutation,0};
    const Extent *span = extent_of(sema,root);
    fwrite(sema->source.data,1,span->begin,file);
    explicit_emit(&x,root);
    fwrite(sema->source.data+span->end,1,sema->source.len-span->end,file);
    fprintf(stderr,"explicit: %zu conversions written as casts; %zu with unnamed target types\n",x.casts,x.unnamed);
    return !ferror(file);
}

/* cesta bind|types [--std=c11|gnu11] [--dialect=gcc|clang] PREPROCESSED-C */
int ce_sema_command(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1],"abi-tables")) {
        const CeAbi *abi = &ce_abi_x86_64_sysv;
        for (int i = 2; i < argc; ++i) {
            if (strncmp(argv[i],"--abi=",6)) {
                fprintf(stderr,"cesta abi-tables: unknown argument %s\n",argv[i]); return 2;
            }
            if (!(abi = ce_abi_named(argv[i]+6))) {
                fprintf(stderr,"cesta abi-tables: unknown ABI %s\n",argv[i]+6); return 2;
            }
        }
        return ce_abi_tables_command(abi);
    }
    CeArena arena = {0}; CeGrammar g = {0}; CeError error = {{0},0};
    CeTokens tokens = {0}; CeForest forest = {.root=-1}; CeDFA dfa = {0}; CeLR lr = {0};
    CeText source = {NULL,0}; CeSema sema = {0}; bool sema_ready = false;
    bool gnu = true, clang = false, dialect_given = false, json_lines = false;
    int result = 1; const char *path = NULL;
    char *records = NULL; size_t records_len = 0; FILE *record_file = NULL;
    const CeAbi *abi = &ce_abi_x86_64_sysv;
    CePPResult preprocessed = {0}; bool have_preprocessed = false;
    int compiler_at = -1;
    for (int i = 2; i < argc; ++i) {
        /* --cc COMPILER FLAGS... SOURCE: Cesta preprocesses the source as that
           compiler would, and every position is reported where it was written. */
        if (!strcmp(argv[i],"--cc")) { compiler_at = i; break; }
        if (!strcmp(argv[i],"--std=c11")) gnu = false;
        else if (!strcmp(argv[i],"--json-lines")) json_lines = true;
        else if (!strcmp(argv[i],"--std=gnu11")) gnu = true;
        else if (!strcmp(argv[i],"--dialect=clang")) { clang = true; dialect_given = true; }
        else if (!strcmp(argv[i],"--dialect=gcc")) { clang = false; dialect_given = true; }
        else if (!strncmp(argv[i],"--mutate=",9)) explicit_mutation = strtoul(argv[i]+9,NULL,10);
        else if (!strncmp(argv[i],"--abi=",6)) {
            if (!(abi = ce_abi_named(argv[i]+6))) { fprintf(stderr,"cesta %s: unknown ABI %s\n",argv[1],argv[i]+6); return 2; }
        }
        else if (!path && argv[i][0] != '-') path = argv[i];
        else { fprintf(stderr,"cesta %s: unknown argument %s\n",argv[1],argv[i]); return 2; }
    }
    if (json_lines && strcmp(argv[1],"bind") && strcmp(argv[1],"types") && strcmp(argv[1],"census")) {
        fprintf(stderr,"cesta %s: --json-lines applies to bind, types and census\n",argv[1]); return 2;
    }
    if (compiler_at >= 0) {
        for (int i = compiler_at+2; i < argc; ++i) {
            if (!strncmp(argv[i],"-std=",5)) gnu = !(argv[i][5] == 'c' || !strncmp(argv[i]+5,"iso",3));
            if (argv[i][0] != '-' && (i == compiler_at+1 || strcmp(argv[i-1],"-I")) ) path = argv[i];
        }
        if (!ce_pp_run(ce_builtin_CPPGNUGrammar(),argc-compiler_at,argv+compiler_at,false,&preprocessed,&error)) {
            fprintf(stderr,"cesta %s: preprocessing: %s\n",argv[1],error.message);
            ce_pp_result_free(&preprocessed); return 1;
        }
        have_preprocessed = true; located = &preprocessed;
        if (!dialect_given) clang = preprocessed.clang;
        path = "<preprocessed>";
    }
    if (!path) { fprintf(stderr,"usage: cesta %s [--std=c11|gnu11] [--dialect=gcc|clang] PREPROCESSED-C\n"
                         "       cesta %s [--abi=ABI] --cc COMPILER COMPILER-FLAGS SOURCE\n",argv[1],argv[1]); return 2; }
    CeTree *tree;
    if (have_preprocessed) source = preprocessed.text;
    if (!ce_builtin_dialect(&g,gnu,clang,&error) || (!have_preprocessed && !ce_read_file(&arena,path,&source,&error)) ||
        !ce_dfa_prepare(&g,&dfa,&error) || !ce_lr_prepare(&g,g.entries[0],&lr,&error) ||
        !ce_dfa_lex(&g,&dfa,source,&tokens,&error) || !ce_lr_parse(&g,&lr,&tokens,&forest,&error) ||
        !ce_tree_from_forest(&g,&forest,&tokens,&arena,&tree,&error)) goto done;
    if (!ce_sema_init(&sema,&g,source,&error)) goto done;
    sema.clang = clang; sema.abi = abi;
    sema_ready = true;
    if (!ce_bind(&sema,tree,&error)) goto done;
    if (json_lines) {
        record_file = open_memstream(&records,&records_len);
        if (!record_file) { ce_error(&error,0,"cannot open analysis record stream"); goto done; }
    }
    FILE *output = record_file ? record_file : stdout;
    if (!strcmp(argv[1],"bind")) { if (!ce_sema_bindings_export(&sema,output)) goto done; }
    else {
        if (!ce_sema_extents(&sema,tree,&tokens,&error) || !ce_elaborate(&sema,tree,&error)) goto done;
        bool ok = !strcmp(argv[1],"types") ? types_export(&sema,tree,output) :
            !strcmp(argv[1],"explicit") ? explicit_export(&sema,tree,output) :
            !strcmp(argv[1],"census") ? census_export(&sema,tree,output) : probe_export(&sema,tree,output);
        if (!ok) goto done;
    }
    if (record_file) {
        bool ok = fclose(record_file) == 0; record_file = NULL;
        if (!ok || !json_records(&arena,(CeText){(unsigned char *)records,records_len},argv[1],&error)) goto done;
    }
    result = ferror(stdout) ? 1 : 0;
done:
    if (record_file) fclose(record_file);
    free(records);
    if (result && error.message[0] && !(have_preprocessed && ce_pp_diagnostic(stderr,&preprocessed,&error))) {
        size_t line = 1, column = 1;
        for (size_t i = 0; i < error.offset && i < source.len; ++i) {
            if (source.data[i] == '\n') { ++line; column = 1; } else ++column;
        }
        fprintf(stderr,"%s:%zu:%zu: %s\n",path,line,column,error.message);
    }
    if (sema_ready) ce_sema_free(&sema);
    if (have_preprocessed) { located = NULL; ce_pp_result_free(&preprocessed); }
    ce_lr_free(&lr); ce_dfa_free(&dfa); ce_forest_free(&forest); ce_tokens_free(&tokens);
    ce_grammar_free(&g); ce_arena_free(&arena);
    return result;
}
