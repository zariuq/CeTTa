#define _GNU_SOURCE
#include "source_compiler_module_v1.h"
#include "source_compiler_provider_v1.h"
#include "src/native_handle.h"
#include "src/eval.h"
#include "src/utf8.h"
#include "src/lang.h"
#include "src/library.h"
#include "module_package_v1.h"
#include "source_parser_plan_v1.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#define KIND "cetta.source-compiler.v1"
#define LANGUAGE_KIND "cetta.language.v1"
#define CURSOR_KIND "cetta.source-cursor.v1"

typedef struct {
    void *library;
    const CettaSourceCompilerProviderV1 *provider;
} Resource;

typedef struct { Arena storage;Atom *components; } Language;
typedef struct { Arena *storage;Atom *state; } Cursor;
static void release_cursor(void *raw) {
    Cursor *cursor=raw;
    if(cursor->storage){arena_free(cursor->storage);free(cursor->storage);}
    free(cursor);
}
static void release_language(void *raw) {
    Language *language=raw;
    arena_free(&language->storage);free(language);
}

static void release(void *value) {
    Resource *r = value;
    if (r) { dlclose(r->library); free(r); }
}

static Atom *failure(Arena *a, Atom *head, const char *message) {
    return atom_error(a, head, atom_string(a, message));
}

static bool text(Atom *value) {
    return value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING;
}
static bool utf8_reply(const char *bytes,size_t length) {
    if(!bytes&&length)return false;
    for(size_t i=0;i<length;) {
        uint32_t scalar,width;
        if(!cetta_utf8_decode_one((const uint8_t *)bytes,length,i,&scalar,&width))return false;
        i+=width;
    }
    return true;
}

Atom *cetta_source_compiler_dispatch_v1(struct CettaLibraryContext *ctx, Arena *a,
                                       Atom *head, Atom **args, uint32_t nargs) {
    if(atom_is_symbol(head,"__cetta_lib_source_cursor_new_v1")) {
        if(nargs!=1u)return failure(a,head,"source cursor expects one state value");
        Cursor *cursor=calloc(1,sizeof(*cursor));
        if(!cursor)return failure(a,head,"source cursor allocation failed");
        cursor->storage=malloc(sizeof(*cursor->storage));
        if(!cursor->storage){free(cursor);return failure(a,head,"source cursor allocation failed");}
        arena_init(cursor->storage);
        cursor->state=atom_deep_copy(cursor->storage,args[0]);
        uint64_t id;
        if(!cursor->state||!cetta_native_handle_alloc(ctx,CURSOR_KIND,cursor,release_cursor,&id)) {
            release_cursor(cursor);return failure(a,head,"source cursor publication failed");
        }
        Atom *owner=cetta_native_handle_owned_atom(ctx,a,CURSOR_KIND,id);
        if(!owner){cetta_native_handle_close(ctx,CURSOR_KIND,id);return failure(a,head,"source cursor owner allocation failed");}
        return owner;
    }
    if(atom_is_symbol(head,"__cetta_lib_source_cursor_state_v1")) {
        Cursor *cursor=nargs==1u?cetta_native_handle_get_owned(ctx,args[0],CURSOR_KIND):NULL;
        if(!cursor)return failure(a,head,"expected a live source cursor");
        return atom_deep_copy(a,cursor->state);
    }
    if(atom_is_symbol(head,"__cetta_lib_source_cursor_replace_v1")) {
        Cursor *cursor=nargs==2u?cetta_native_handle_get_owned(ctx,args[0],CURSOR_KIND):NULL;
        if(!cursor)return failure(a,head,"expected a live source cursor and replacement value");
        Arena *next=malloc(sizeof(*next));
        if(!next)return failure(a,head,"source cursor replacement failed");
        arena_init(next);
        Atom *state=atom_deep_copy(next,args[1]);
        if(!state){arena_free(next);free(next);return failure(a,head,"source cursor replacement failed");}
        /* Copies handed to callers have their own arena. Install a complete
         * replacement before releasing the previous retained state. */
        Arena *previous=cursor->storage;cursor->storage=next;cursor->state=state;
        arena_free(previous);free(previous);
        return atom_bool(a,true);
    }
    if(atom_is_symbol(head,"__cetta_lib_source_execution_language_v1")&&nargs==1u)
        return atom_string(a,cetta_language_canonical_name(eval_current_language_id()));
    if(atom_is_symbol(head,"__cetta_lib_source_execution_profile_v1")&&nargs==1u)
        return atom_string(a,ctx->session.profile?ctx->session.profile->name:"default");
    if(atom_is_symbol(head,"__cetta_lib_source_language_components_v1")) {
        Language *language=nargs==1u?cetta_native_handle_get_owned(ctx,args[0],LANGUAGE_KIND):NULL;
        if(!language)return failure(a,head,"expected a live bound language value");
        return atom_deep_copy(a,language->components);
    }
    if(atom_is_symbol(head,"__cetta_lib_source_language_bind_v1")) {
        Resource *compiler=nargs==5u?cetta_native_handle_get_owned(ctx,args[2],KIND):NULL;
        if(!compiler)return failure(a,head,"language binding requires an owned compiler");
        static const char *keys[]={"syntax","core","parserPlan","execution","compiler"};
        Atom *specs[5];const char *texts[5];size_t lengths[5];
        for(size_t i=0;i<5u;i++) {
            Atom *export=cetta_module_package_export_v1(a,args[0],atom_string(a,keys[i]));
            if(!export||export->kind!=ATOM_EXPR||export->expr.len!=3u||
                !atom_is_symbol(export->expr.elems[0],"ModuleExportV1"))
                return failure(a,head,"language component is unavailable");
            specs[i]=export->expr.elems[2];
            texts[i]=atom_to_parseable_string(a,specs[i]);
            if(!texts[i])return failure(a,head,"language component has no data representation");
            lengths[i]=strlen(texts[i]);
        }
        if(!atom_eq(specs[3],args[4])||!atom_eq(specs[4],args[3])||
            !cetta_source_parser_plan_matches_v1(ctx,args[1],specs[2]))
            return failure(a,head,"language handles do not realize the declared components");
        char *receipt=NULL;size_t length=0;
        int ok=compiler->provider->call("admit",texts,lengths,5u,&receipt,&length);
        static const char accepted[]="SourceBindingAcceptedV1";
        bool admitted=ok&&receipt&&length==sizeof(accepted)-1u&&!memcmp(receipt,accepted,length);
        Atom *reason=receipt&&utf8_reply(receipt,length)?atom_string_n(a,receipt,length):
            atom_string(a,"compiler supplies no valid component binding");
        if(receipt)compiler->provider->release(receipt);
        if(!admitted) {
            Atom *fields[]={atom_symbol(a,"SourceBindingRefusedV1"),reason};
            return atom_expr(a,fields,2u);
        }
        Language *language=calloc(1,sizeof(*language));
        if(!language)return failure(a,head,"language binding allocation failed");
        arena_init(&language->storage);
        Atom *fields[]={atom_symbol(a,"LangComponentsV1"),args[0],args[1],args[2],args[3],args[4]};
        language->components=atom_deep_copy(&language->storage,atom_expr(a,fields,6u));
        uint64_t id;
        if(!language->components||!cetta_native_handle_alloc(ctx,LANGUAGE_KIND,language,release_language,&id)) {
            release_language(language);return failure(a,head,"language binding publication failed");
        }
        Atom *owner=cetta_native_handle_owned_atom(ctx,a,LANGUAGE_KIND,id);
        if(!owner){cetta_native_handle_close(ctx,LANGUAGE_KIND,id);return failure(a,head,"language owner allocation failed");}
        return owner;
    }
    if (atom_is_symbol(head, "__cetta_lib_source_host_value_v1")) {
        bool valid=false;
        if(nargs==2u&&text(args[0])&&args[1]&&args[1]->kind==ATOM_GROUNDED) {
            Atom *v=args[1];const char *kind=args[0]->ground.sval;
            if(!strcmp(kind,"string"))valid=v->ground.gkind==GV_STRING;
            if(!strcmp(kind,"number")||!strcmp(kind,"unsigned")) {
                valid=v->ground.gkind==GV_INT||v->ground.gkind==GV_BIGINT;
                if(valid&&!strcmp(kind,"unsigned"))
                    valid=v->ground.gkind==GV_INT?v->ground.ival>=0:atom_bigint_cstr(v)[0]!='-';
            }
        }
        return atom_bool(a,valid);
    }
    if (atom_is_symbol(head, "__cetta_lib_source_natural_v1")) {
        bool valid = nargs == 1u && args[0] && args[0]->kind == ATOM_GROUNDED &&
            ((args[0]->ground.gkind == GV_INT && args[0]->ground.ival >= 0) ||
             (args[0]->ground.gkind == GV_BIGINT && atom_bigint_cstr(args[0])[0] != '-'));
        return atom_bool(a, valid);
    }
    if (atom_is_symbol(head, "__cetta_lib_source_compiler_open_v1")) {
        if (nargs != 1u || !text(args[0]) || strlen(args[0]->ground.sval) != atom_string_len(args[0]))
            return failure(a, head, "source compiler load expects a file path");
        /* Compiled modules can retain process-global initialized constants.
         * Pin code until process exit; closing releases the owning handle,
         * without promising to unload those module globals. */
        void *library = dlopen(args[0]->ground.sval, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
        if (!library) return failure(a, head, dlerror());
        CettaSourceCompilerEntryV1 entry;
        void *symbol = dlsym(library, "cetta_source_compiler_v1");
        memcpy(&entry, &symbol, sizeof(entry));
        const CettaSourceCompilerProviderV1 *provider = entry ? entry() : NULL;
        if (!provider || provider->abi_version != 1u || !provider->name || !provider->call || !provider->release) {
            dlclose(library);
            return failure(a, head, "file does not supply a source compiler v1 provider");
        }
        Resource *resource = malloc(sizeof(*resource));
        if (!resource) { dlclose(library); return failure(a, head, "source compiler allocation failed"); }
        *resource = (Resource){library, provider};
        uint64_t id;
        if (!cetta_native_handle_alloc(ctx, KIND, resource, release, &id)) {
            release(resource); return failure(a, head, "source compiler handle allocation failed");
        }
        Atom *value = cetta_native_handle_owned_atom(ctx, a, KIND, id);
        if (!value) { cetta_native_handle_close(ctx, KIND, id); return failure(a, head, "source compiler owner allocation failed"); }
        return value;
    }
    if (atom_is_symbol(head, "__cetta_lib_source_compiler_close_v1")) {
        uint64_t id;
        if (nargs != 1u || !cetta_native_handle_get_owned(ctx, args[0], KIND) ||
            !cetta_native_handle_arg(args[0], KIND, &id))
            return failure(a, head, "source compiler close expects a live owning handle");
        return atom_bool(a, cetta_native_handle_close(ctx, KIND, id));
    }
    if (!atom_is_symbol(head, "__cetta_lib_source_compiler_call_v1")) return NULL;
    Resource *resource = nargs == 3u ? cetta_native_handle_get_owned(ctx, args[0], KIND) : NULL;
    if (!resource || !text(args[1]) || strlen(args[1]->ground.sval) != atom_string_len(args[1]))
        return failure(a, head, "source compiler call expects a live compiler, operation and argument list");
    const char **arguments = NULL;
    size_t *lengths = NULL;
    size_t count = 0;
    Atom *list = args[2];
    while (list && list->kind == ATOM_EXPR && list->expr.len == 3u && atom_is_symbol(list->expr.elems[0], "LCons")) {
        Atom *argument = list->expr.elems[1];
        if (!text(argument) || count == SIZE_MAX / sizeof(*arguments) - 1u) goto malformed;
        const char **next = realloc(arguments, (count + 1u) * sizeof(*arguments));
        if (!next) goto malformed;
        arguments = next;
        size_t *next_lengths = realloc(lengths, (count + 1u) * sizeof(*lengths));
        if (!next_lengths) goto malformed;
        lengths = next_lengths;
        arguments[count] = argument->ground.sval;
        lengths[count++] = atom_string_len(argument);
        list = list->expr.elems[2];
    }
    if (!atom_is_symbol(list, "LNil")) goto malformed;
    char *response = NULL;
    size_t length = 0;
    int completed = resource->provider->call(args[1]->ground.sval, arguments, lengths, count, &response, &length);
    free(arguments); free(lengths);
    if (!completed || !response) {
        if(response)resource->provider->release(response);
        return failure(a, head, "source compiler operation did not return a value");
    }
    if(!utf8_reply(response,length)) {
        resource->provider->release(response);
        return failure(a,head,"source compiler returned malformed UTF-8");
    }
    Atom *value = atom_string_n(a, response, length);
    resource->provider->release(response);
    return value;
malformed:
    free(arguments); free(lengths);
    return failure(a, head, "source compiler arguments must be an LCons/LNil list of strings");
}
