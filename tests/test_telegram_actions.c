#define _POSIX_C_SOURCE 200809L
#include "telegram_action.h"
#include "library.h"
#include "parser.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Arena persistent, scratch;
static const int64_t chats[]={42,-100,INT64_MIN};
static CettaTelegramActionPolicy policy={chats,3,15};
static unsigned checks;
static Atom *parse(const char *text) {
    size_t pos=0; Atom *a=parse_sexpr(&persistent,text,&pos); assert(a && pos==strlen(text)); return a;
}
static void rejected(Atom *p) {
    CettaTelegramPlan out={0};
    assert(!cetta_telegram_action_validate(&policy,p,NULL));
    assert(!cetta_telegram_action_plan(&policy,&scratch,p,NULL,&out) && !out.body && !out.method); ++checks;
}
static CettaTelegramPlan accepted(Atom *p, const char *method) {
    CettaTelegramPlan out={0};
    assert(cetta_telegram_action_validate(&policy,p,NULL));
    assert(cetta_telegram_action_plan(&policy,&scratch,p,NULL,&out));
    assert(!strcmp(out.method,method) && !strcmp(out.content_type,"application/json") && out.size==strlen(out.body));
    ++checks; return out;
}
static Atom *send_text(const char *text) {
    Atom *parts[]={atom_symbol(&persistent,"telegram:send-text"),atom_int(&persistent,1),atom_int(&persistent,42),
        atom_int(&persistent,0),atom_int(&persistent,0),atom_string(&persistent,text),atom_string(&persistent,"plain")};
    return atom_expr(&persistent,parts,7);
}
int main(int argc, char **argv) {
    (void)argc;
    SymbolTable symbols; symbol_table_init(&symbols); symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols=&symbols;
    VarInternTable vars; var_intern_init(&vars); g_var_intern=&vars;
    arena_init(&persistent); arena_init(&scratch);
    CettaTelegramPlan out=accepted(parse("(telegram:send-text 1 42 17 3 \"hello\" \"plain\")"),"sendMessage");
    assert(!strcmp(out.body,"{\"chat_id\":42,\"message_thread_id\":17,\"reply_parameters\":{\"message_id\":3},\"text\":\"hello\"}"));
    out=accepted(parse("(telegram:edit-text 1 -100 9 \"<b>x</b>\" \"HTML\")"),"editMessageText");
    assert(!strcmp(out.body,"{\"chat_id\":-100,\"message_id\":9,\"text\":\"<b>x</b>\",\"parse_mode\":\"HTML\"}"));
    out=accepted(parse("(telegram:delete-message 1 -9223372036854775808 9223372036854775807)"),"deleteMessage");
    assert(!strcmp(out.body,"{\"chat_id\":-9223372036854775808,\"message_id\":9223372036854775807}"));
    out=accepted(send_text("\"\\\n\r\t\001ž🌿"),"sendMessage");
    assert(!strcmp(out.body,"{\"chat_id\":42,\"text\":\"\\\"\\\\\\u000a\\u000d\\u0009\\u0001ž🌿\"}"));
    out=accepted(send_text("\",\"chat_id\":123,\"text\":\"injected"),"sendMessage");
    assert(!strcmp(out.body,"{\"chat_id\":42,\"text\":\"\\\",\\\"chat_id\\\":123,\\\"text\\\":\\\"injected\"}"));
    accepted(parse("(telegram:send-text 1 42 0 0 \"x\" \"MarkdownV2\")"),"sendMessage");
    /* Keyboards and callback answers. */
    out=accepted(parse("(telegram:send-text 1 42 0 0 \"modes\" \"plain\" (telegram:keyboard (((telegram:button \"● iter\" \"mode:iter\") (telegram:button \"agent\" \"mode:agent\")) ((telegram:button \"x\" \"y\")))))"),"sendMessage");
    assert(!strcmp(out.body,"{\"chat_id\":42,\"text\":\"modes\",\"reply_markup\":{\"inline_keyboard\":[[{\"text\":\"● iter\",\"callback_data\":\"mode:iter\"},{\"text\":\"agent\",\"callback_data\":\"mode:agent\"}],[{\"text\":\"x\",\"callback_data\":\"y\"}]]}}"));
    out=accepted(parse("(telegram:edit-text 1 42 9 \"m\" \"plain\" (telegram:keyboard (((telegram:button \"a\\\"b\" \"c\\\\d\")))))"),"editMessageText");
    assert(!strcmp(out.body,"{\"chat_id\":42,\"message_id\":9,\"text\":\"m\",\"reply_markup\":{\"inline_keyboard\":[[{\"text\":\"a\\\"b\",\"callback_data\":\"c\\\\d\"}]]}}"));
    out=accepted(parse("(telegram:answer-callback 1 42 \"4382bfdwdsb323b2d9\" \"mode: iter\")"),"answerCallbackQuery");
    assert(!strcmp(out.body,"{\"callback_query_id\":\"4382bfdwdsb323b2d9\",\"text\":\"mode: iter\"}"));
    out=accepted(parse("(telegram:answer-callback 1 42 \"7\" \"\")"),"answerCallbackQuery");
    assert(!strcmp(out.body,"{\"callback_query_id\":\"7\"}"));
    const char *bad_menus[]={
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard ()))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (())))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:button \"\" \"d\")))))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:button \"a\" \"\")))))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:button \"a\\nb\" \"d\")))))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:button \"a\" \"d\" x)))))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:url \"a\" \"https://x\")))))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard ((telegram:button \"a\" \"d\"))))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:button a \"d\")))))",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" ())",
        "(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:button \"a\" \"d\")))) extra)",
        "(telegram:edit-text 1 42 9 \"m\" \"plain\" (telegram:keyboard ()))",
        "(telegram:answer-callback 1 43 \"7\" \"x\")", "(telegram:answer-callback 1 42 \"\" \"x\")",
        "(telegram:answer-callback 1 42 \"a b\" \"x\")", "(telegram:answer-callback 1 42 \"a\\\"b\" \"x\")",
        "(telegram:answer-callback 1 42 7 \"x\")", "(telegram:answer-callback 1 42 \"7\")",
        "(telegram:answer-callback 1 42 \"7\" \"x\" extra)", "(telegram:answer-callback 2 42 \"7\" \"x\")"
    };
    for (size_t i=0;i<sizeof(bad_menus)/sizeof(*bad_menus);++i) rejected(parse(bad_menus[i]));
    {   /* Limits: 8 buttons to a row, 100 in all, labels of 64 scalars and
         * data of 64 bytes, toasts of 200 scalars. */
        char *k=malloc(65536), *p;
        for (int n=8;n<=9;++n) {
            p=k+sprintf(k,"(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard ((");
            for (int i=0;i<n;++i) p+=sprintf(p,"(telegram:button \"a\" \"d\")");
            strcpy(p,"))))");
            if (n==8) accepted(parse(k),"sendMessage"); else rejected(parse(k));
        }
        for (int n=100;n<=101;++n) {
            p=k+sprintf(k,"(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (");
            for (int i=0;i<n;++i) p+=sprintf(p,"((telegram:button \"a\" \"d\"))");
            strcpy(p,")))");
            if (n==100) accepted(parse(k),"sendMessage"); else rejected(parse(k));
        }
        for (int n=64;n<=65;++n) {
            p=k+sprintf(k,"(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:button \"");
            for (int i=0;i<n;++i) p+=sprintf(p,"🌿");
            strcpy(p,"\" \"d\")))))");
            if (n==64) accepted(parse(k),"sendMessage"); else rejected(parse(k));
            p=k+sprintf(k,"(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (((telegram:button \"a\" \"");
            for (int i=0;i<n;++i) *p++='d';
            strcpy(p,"\")))))");
            if (n==64) accepted(parse(k),"sendMessage"); else rejected(parse(k));
        }
        for (int n=200;n<=201;++n) {
            p=k+sprintf(k,"(telegram:answer-callback 1 42 \"7\" \"");
            for (int i=0;i<n;++i) p+=sprintf(p,"🌿");
            strcpy(p,"\")");
            if (n==200) accepted(parse(k),"answerCallbackQuery"); else rejected(parse(k));
        }
        for (int extra=4;extra<=5;++extra) {
            p=k+sprintf(k,"(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (");
            for (int r=0;r<13;++r) {
                *p++='(';
                for (int b=0;b<(r==12?extra:8);++b) p+=sprintf(p,"(telegram:button \"a\" \"d\")");
                *p++=')';
            }
            strcpy(p,")))");
            if (extra==4) accepted(parse(k),"sendMessage"); else rejected(parse(k));
        }
        /* A full keyboard of the longest labels and data, all escaped, fits
         * the planned body. */
        p=k+sprintf(k,"(telegram:send-text 1 42 0 0 \"m\" \"plain\" (telegram:keyboard (");
        for (int i=0;i<100;++i) {
            p+=sprintf(p,"((telegram:button \"");
            for (int j=0;j<64;++j) p+=sprintf(p,"🌿");
            p+=sprintf(p,"\" \"");
            for (int j=0;j<32;++j) p+=sprintf(p,"\\\"");
            p+=sprintf(p,"\"))");
        }
        strcpy(p,")))");
        out=accepted(parse(k),"sendMessage"); assert(out.size>100*(256+64)); free(k);
    }
    const char *bad[]={
        "(telegram:send-text 1 43 0 0 \"x\" \"plain\")", "(telegram:send-text 1 0 0 0 \"x\" \"plain\")",
        "(telegram:send-text 2 42 0 0 \"x\" \"plain\")", "(telegram:send-text 1.0 42 0 0 \"x\" \"plain\")",
        "(telegram:send-text 1 42.0 0 0 \"x\" \"plain\")", "(telegram:send-text 1 \"42\" 0 0 \"x\" \"plain\")",
        "(telegram:send-text 1 42 -1 0 \"x\" \"plain\")", "(telegram:send-text 1 42 0 -1 \"x\" \"plain\")",
        "(telegram:send-text 1 42 0 0 \"\" \"plain\")", "(telegram:send-text 1 42 0 0 x \"plain\")",
        "(telegram:send-text 1 42 0 0 \"x\" plain)", "(telegram:send-text 1 42 0 0 \"x\" \"html\")",
        "(telegram:send-text 1 42 0 0 \"x\" \"plain\" extra)", "(telegram:edit-text 1 42 0 \"x\" \"plain\")",
        "(telegram:delete-message 1 42 -1)", "(telegram:delete-message 1 42 1.0)",
        "(setWebhook 1 42 1)", "(getUpdates 1 42 1)", "(deleteWebhook 1 42 1)", "(logOut 1 42 1)", "(close 1 42 1)",
        "(telegram:delete-message 1 $chat 1)", "(telegram:send-text 1 42 0 0 (io:submit a) \"plain\")", "()", "bad"
    };
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) rejected(parse(bad[i]));
    rejected(NULL);
    const char *utf8[]={"\x80", "\xc0\xaf", "\xc1\xbf", "\xe0\x80\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf0\x9f\x8c", "\xc2x"};
    for (size_t i=0;i<sizeof(utf8)/sizeof(*utf8);++i) rejected(send_text(utf8[i]));
    char *large=malloc(16389); assert(large);
    memset(large,'a',4096); large[4096]=0; accepted(send_text(large),"sendMessage");
    large[4096]='a'; large[4097]=0; rejected(send_text(large));
    for (size_t i=0;i<4097;++i) memcpy(large+4*i,"🌿",4);
    large[16384]=0; accepted(send_text(large),"sendMessage");
    memcpy(large+16384,"🌿",4); large[16388]=0; rejected(send_text(large)); free(large);
    for (unsigned mask=0;mask<16;++mask) {
        policy.methods=mask;
        assert(cetta_telegram_action_validate(&policy,parse("(telegram:answer-callback 1 42 \"7\" \"x\")"),NULL)==!!(mask&8)); ++checks;
        assert(cetta_telegram_action_validate(&policy,send_text("x"),NULL)==!!(mask&1));
        assert(cetta_telegram_action_validate(&policy,parse("(telegram:edit-text 1 42 1 \"x\" \"plain\")"),NULL)==!!(mask&2));
        assert(cetta_telegram_action_validate(&policy,parse("(telegram:delete-message 1 42 1)"),NULL)==!!(mask&4)); checks+=3;
    }
    policy.methods=8; rejected(send_text("x")); policy.methods=16; rejected(send_text("x")); policy.methods=15;
    policy.chat_count=0; rejected(send_text("x")); policy.chat_count=129; rejected(send_text("x")); policy.chat_count=3;
    assert(!cetta_telegram_action_validate(NULL,send_text("x"),NULL)); ++checks;
    // Dispatch must not rely on acceptance's earlier policy check.
    Atom *previous=send_text("accepted earlier"); assert(cetta_telegram_action_validate(&policy,previous,NULL));
    policy.methods=0; rejected(previous); policy.methods=15;
    Space program; space_init(&program); Registry registry; registry_init(&registry);
    registry_bind(&registry,"&self",atom_space(&persistent,&program));
    CettaLibraryContext context; cetta_library_context_init(&context); cetta_library_context_set_exec_path(&context,argv[0]);
    cetta_eval_session_init_he_extended(&context.session); eval_set_library_context(&context);
    Atom *error=NULL;
    assert(cetta_library_import_module(&context,"durable:telegram_actions",&program,false,&scratch,&persistent,&registry,1000000,NULL,&error));
    const char *expressions[]={"(durable:telegram:send-text 0 42 0 0 \"x\" \"plain\" reply)",
        "(durable:telegram:edit-text 0 42 1 \"x\" \"plain\" reply)","(durable:telegram:delete-message 0 42 1 reply)"};
    for (size_t i=0;i<3;++i) {
        EvalOutcome result; eval_outcome_init(&result);
        eval_top_speculative(&context,&program,&scratch,&persistent,&registry,parse(expressions[i]),1000,&result);
        assert(result.completion==CETTA_EVAL_COMPLETE && !result.effect_denials && result.results.len==1);
        Atom *v=result.results.items[0]; assert(v->kind==ATOM_EXPR && v->expr.len==4 && atom_is_symbol(v->expr.elems[0],"host:send"));
        assert(cetta_telegram_action_validate(&policy,v->expr.elems[2],NULL)); ++checks; eval_outcome_free(&result);
    }
    cetta_library_context_free(&context); eval_set_library_context(NULL); registry_free(&registry); space_free(&program);
    printf("Telegram actions: %u validation, Unicode, JSON, authority and pure-constructor checks passed\n",checks);
    arena_free(&scratch); arena_free(&persistent); var_intern_free(&vars); symbol_table_free(&symbols); g_symbols=NULL; g_var_intern=NULL;
}
