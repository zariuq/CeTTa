#include "source_parser_plan_v1.h"
#include "src/native_handle.h"
#include "src/native_sha256.h"
#include "finite_horn_ground_term_v1.h"
#include "parser_pack_gll_v1.h"
#include "parser_pack_glr_v1.h"
#include "module_value_v1.h"
#include <stdlib.h>
#include <string.h>
#include "source_family_selection_v1.h"
#include "source_plan_identity_v1.h"

#define PLAN_KIND "cetta.source-parser-plan.v1"
#define FOREST_KIND "cetta.source-parser-forest.v1"

typedef struct {
    Arena storage;
    PPABIV1Pack pack;
    Atom *start;
    SourceFamilyPolicy selection;
    Atom *definition[4];
} Plan;

typedef struct {
    Arena storage;
    Atom *parent;
    PPNativeV1Result result;
} Forest;

static void plan_free(void *raw) {
    Plan *p = raw;
    free(p->selection.filters);
    ppabi_v1_pack_free(&p->pack);
    arena_free(&p->storage);
    free(p);
}

static void forest_free(void *raw) {
    Forest *f = raw;
    ppnative_v1_result_free(&f->result);
    arena_free(&f->storage);
    free(f);
}

static bool string(Atom *a) {
    return a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING;
}

static Atom *error(Arena *a, Atom *head, const char *message) {
    return atom_error(a, head, atom_string(a, message));
}

static Atom *byte_hex(Arena *a, const uint8_t *bytes, size_t length) {
    if(length>(SIZE_MAX-1u)/2u)return NULL;
    char *text=malloc(length*2u+1u);
    if(!text)return NULL;
    static const char digits[]="0123456789abcdef";
    for(size_t i=0u;i<length;i++) {
        text[2u*i]=digits[bytes[i]>>4u];
        text[2u*i+1u]=digits[bytes[i]&15u];
    }
    text[length*2u]=0;
    Atom *value=atom_string(a,text);free(text);return value;
}

static bool rows(Arena *storage, Atom *text, Atom ***out, size_t *count, char digest[65], char *err, size_t size) {
    if (!string(text)) return false;
    const uint8_t *bytes = (const uint8_t *)text->ground.sval;
    size_t length = atom_string_len(text), begin = 0;
    cetta_native_sha256_hex(bytes, length, digest);
    *out = NULL; *count = 0;
    for (size_t end = 0; end <= length; ++end) {
        if (end < length && bytes[end] != '\n') continue;
        if (end > begin) {
            Atom *value = NULL;
            if (!fh_ground_term_v1_parse(storage, bytes + begin, end - begin, &value, err, size)) return false;
            if (*count >= SIZE_MAX / sizeof(**out)) return false;
            Atom **next = realloc(*out, (*count + 1u) * sizeof(**out));
            if (!next) return false;
            *out = next; next[(*count)++] = value;
        }
        begin = end + 1u;
    }
    return true;
}

static bool parse_data(Arena *a, Atom *text, Atom **value, char *err, size_t size) {
    return string(text) && fh_ground_term_v1_parse(a, (const uint8_t *)text->ground.sval,
        atom_string_len(text), value, err, size);
}

static bool finish(Forest *f, const Plan *p, uint32_t limit,uint32_t depth, char *err, size_t size) {
    uint32_t *saved = f->result.forest.roots, count = f->result.forest.root_len;
    uint32_t *whole = calloc(count ? count : 1u, sizeof(*whole)), n = 0;
    if (!whole) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (saved[i] >= f->result.forest.node_len) { free(whole); return false; }
        const CettaLpNativeUtf8ForestNode *node = &f->result.forest.nodes[saved[i]];
        if (!node->scalar_left && node->scalar_right == f->result.forest.scalar_len) whole[n++] = saved[i];
    }
    f->result.forest.roots = whole; f->result.forest.root_len = n;
    PPNativeV1FamilySelection selection = {source_family_accepts, (void *)&p->selection};
    bool ok = ppnative_v1_finish_values_selected(&f->result, &p->pack, NULL, depth,
                                                 limit, &selection, err, size);
    f->result.forest.roots = saved; f->result.forest.root_len = count;
    free(whole);
    return ok;
}

bool cetta_source_parser_plan_matches_v1(struct CettaLibraryContext *ctx,Atom *owner,Atom *record) {
    Plan *p=cetta_native_handle_get_owned(ctx,PLAN_KIND,owner);
    if(!p)return false;
    static const char *keys[]={"productions","classes","start","selection"};
    Arena a;arena_init(&a);bool same=true;
    for(size_t i=0;i<4u&&same;i++) {
        Atom *field=cetta_module_portable_field_v1(&a,record,atom_string(&a,keys[i]));
        same=field&&field->kind==ATOM_EXPR&&field->expr.len==2u&&
            atom_is_symbol(field->expr.elems[0],"ModuleValueFoundV1")&&
            atom_eq(p->definition[i],field->expr.elems[1]);
    }
    arena_free(&a);return same;
}

Atom *cetta_source_parser_plan_dispatch_v1(struct CettaLibraryContext *ctx, Arena *a,
                                          Atom *head, Atom **args, uint32_t nargs) {
    char err[512] = {0};
    if (atom_is_symbol(head, "__cetta_lib_source_plan_evidence_v1")) {
        Forest *f=nargs==1u?cetta_native_handle_get_owned(ctx,FOREST_KIND,args[0]):NULL;
        Plan *p=f?cetta_native_handle_get_owned(ctx,PLAN_KIND,f->parent):NULL;
        if(!p)return error(a,head,"source evidence requires a live forest and its parser");
        size_t size=0u,written=0u,identity_size=0u;
        uint8_t *bytes=NULL,*identity=NULL;
        bool ok=cetta_lp_native_utf8_forest_wire_size(&f->result.forest,&size,err,sizeof(err));
        if(ok){bytes=malloc(size?size:1u);ok=bytes!=NULL;}
        if(ok)ok=cetta_lp_native_utf8_forest_wire_write(&f->result.forest,bytes,size,&written,err,sizeof(err))&&written==size;
        if(ok)ok=source_plan_identity_bytes(&p->pack,&identity,&identity_size,err,sizeof(err));
        Atom *forest_wire=ok?byte_hex(a,bytes,size):NULL;
        Atom *identity_wire=ok?byte_hex(a,identity,identity_size):NULL;
        free(bytes);free(identity);
        if(!forest_wire||!identity_wire)return error(a,head,err[0]?err:"source evidence encoding failed");
        Atom *parts[]={atom_symbol(a,"SourceParserEvidenceV1"),forest_wire,identity_wire};
        return atom_expr(a,parts,3u);
    }
    if (atom_is_symbol(head, "__cetta_lib_source_plan_load_v1")) {
        if (nargs != 4u) return error(a, head, "source plan requires production/class rows, start and selection data");
        Plan *p = calloc(1, sizeof(*p));
        if (!p) return error(a, head, "source plan allocation failed");
        arena_init(&p->storage); ppabi_v1_pack_init(&p->pack);
        Atom **productions = NULL, **classes = NULL, *policy = NULL;
        size_t production_count = 0, class_count = 0;
        char pd[65], cd[65], adapter[65];
        static const uint8_t tag[] = "GSLTDirectParserPackV1";
        cetta_native_sha256_hex(tag, sizeof(tag) - 1u, adapter);
        bool ok = rows(&p->storage, args[0], &productions, &production_count, pd, err, sizeof(err)) &&
            rows(&p->storage, args[1], &classes, &class_count, cd, err, sizeof(err)) &&
            ppabi_v1_pack_load_structural(&p->pack, productions, production_count, classes, class_count,
                pd, adapter, cd, err, sizeof(err)) &&
            parse_data(&p->storage, args[2], &p->start, err, sizeof(err)) &&
            ppabi_v1_pack_start_is_closed(&p->pack, p->start, err, sizeof(err)) &&
            parse_data(&p->storage, args[3], &policy, err, sizeof(err)) &&
            source_policy_read(&p->selection, &p->pack, policy);
        free(productions); free(classes);
        if (!ok) { plan_free(p); return error(a, head, err[0] ? err : "invalid source plan data"); }
        for(size_t i=0;i<4u;i++)p->definition[i]=atom_deep_copy(&p->storage,args[i]);
        uint64_t id;
        if (!cetta_native_handle_alloc(ctx, PLAN_KIND, p, plan_free, &id)) {
            plan_free(p); return error(a, head, "source plan owner allocation failed");
        }
        Atom *value = cetta_native_handle_owned_atom(ctx, a, PLAN_KIND, id);
        if (!value) { cetta_native_handle_close(ctx, PLAN_KIND, id); return error(a, head, "source plan publication failed"); }
        return value;
    }
    if (atom_is_symbol(head, "__cetta_lib_source_plan_forest_v1")) {
        Forest *f = nargs == 2u ? cetta_native_handle_get_owned(ctx,FOREST_KIND,args[0]) : NULL;
        Plan *p = f ? cetta_native_handle_get_owned(ctx,PLAN_KIND,f->parent) : NULL;
        if (!p) return error(a, head, "forest inspection requires a live forest and its parser");
        if (!args[1] || args[1]->kind != ATOM_GROUNDED || args[1]->ground.gkind != GV_INT ||
            args[1]->ground.ival <= 0 || args[1]->ground.ival > UINT32_MAX)
            return error(a, head, "forest inspection requires a positive item allowance");
        if (!ppnative_v1_materialize_canonical_forest(&f->result, &p->pack, p->start,
                (uint32_t)args[1]->ground.ival, err, sizeof(err)))
            return error(a, head, err[0] ? err : "forest materialization failed");
        return atom_deep_copy(a, f->result.canonical_forest);
    }
    if (atom_is_symbol(head, "__cetta_lib_source_plan_close_v1") ||
        atom_is_symbol(head, "__cetta_lib_source_forest_close_v1")) {
        const char *kind = atom_is_symbol(head, "__cetta_lib_source_plan_close_v1") ? PLAN_KIND : FOREST_KIND;
        uint64_t id;
        if (nargs != 1u || !cetta_native_handle_get_owned(ctx,kind,args[0]) ||
            !cetta_native_handle_arg(args[0], kind, &id))
            return error(a, head, "close requires a live owning source resource");
        return atom_bool(a, cetta_native_handle_close(ctx, kind, id));
    }
    if (!atom_is_symbol(head, "__cetta_lib_source_plan_parse_v1")) return NULL;
    Plan *p = nargs >=4u && nargs<=6u ? cetta_native_handle_get_owned(ctx,PLAN_KIND,args[0]) : NULL;
    if (!p || !string(args[1]) || !args[2] || args[2]->kind != ATOM_GROUNDED || args[2]->ground.gkind != GV_INT ||
        args[2]->ground.ival <= 0 || args[2]->ground.ival > UINT32_MAX ||
        !args[3] || args[3]->kind != ATOM_GROUNDED || args[3]->ground.gkind != GV_INT ||
        args[3]->ground.ival <= 0 || args[3]->ground.ival > UINT32_MAX)
        return error(a, head, "source parsing requires a live plan, text, positive work and result allowances");
    bool glr=nargs>=5u&&atom_is_symbol(args[4],"SourceGLRV1");
    if(nargs>=5u&&!glr&&!atom_is_symbol(args[4],"SourceGLLV1"))
        return error(a,head,"expected SourceGLLV1 or SourceGLRV1");
    uint32_t depth=4096u;
    if(nargs==6u) {
        if(!args[5]||args[5]->kind!=ATOM_GROUNDED||args[5]->ground.gkind!=GV_INT||
            args[5]->ground.ival<=0||args[5]->ground.ival>UINT32_MAX)
            return error(a,head,"expected a positive replay depth");
        depth=(uint32_t)args[5]->ground.ival;
    }
    Forest *f = calloc(1, sizeof(*f));
    if (!f) return error(a, head, "forest allocation failed");
    arena_init(&f->storage); ppnative_v1_result_init(&f->result);
    f->parent = atom_deep_copy(&f->storage, args[0]);
    bool ok = f->parent && (glr?ppglr_v1_recognize:ppgll_v1_recognize)(&p->pack, p->start,
        (const uint8_t *)args[1]->ground.sval, atom_string_len(args[1]),
        (uint32_t)args[2]->ground.ival, &f->result, err, sizeof(err));
    if (ok && f->result.outcome == PPNATIVE_V1_COMPLETED)
        ok = finish(f, p, (uint32_t)args[3]->ground.ival,depth, err, sizeof(err));
    if (!ok) { forest_free(f); return error(a, head, err[0] ? err : "source parsing failed"); }
    uint64_t id;
    if (!cetta_native_handle_alloc(ctx, FOREST_KIND, f, forest_free, &id)) {
        forest_free(f); return error(a, head, "forest owner allocation failed");
    }
    Atom *owner = cetta_native_handle_owned_atom(ctx, a, FOREST_KIND, id);
    if (!owner) { cetta_native_handle_close(ctx, FOREST_KIND, id); return error(a, head, "forest publication failed"); }
    size_t n = f->result.semantic_result_len;
    Atom **values = malloc((n + 1u) * sizeof(*values));
    if (!values) { cetta_native_handle_close(ctx, FOREST_KIND, id); return error(a, head, "result allocation failed"); }
    values[0] = atom_symbol(a, "SourceValuesV1");
    for (size_t i = 0; i < n; ++i) values[i + 1u] = atom_deep_copy(a, f->result.semantic_results[i]);
    Atom *list = atom_expr(a, values, n + 1u); free(values);
    Atom *fields[] = {atom_symbol(a, "SourceParseV1"), atom_int(a, f->result.outcome),
        atom_bool(a, f->result.accepted), list, owner};
    return atom_expr(a, fields, 5u);
}
