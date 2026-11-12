#include "atom.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Restrict the fault sweep to this codec's new allocations. The existing JSON
 * parser/emitter and arena allocations have their own independent controls. */
static long fail_at = -1, allocations;
static void *live[4096];
static size_t live_count;
static bool interrupted;
static void remember(void *p) {
    if (p) { assert(live_count < 4096); live[live_count++] = p; }
}
static void forget(void *p) {
    for (size_t i = 0; i < live_count; ++i)
        if (live[i] == p) { live[i] = live[--live_count]; return; }
}
static bool refuse(void) { return fail_at >= 0 && allocations++ == fail_at; }
static void *probe_malloc(size_t n) {
    if (refuse()) return NULL;
    void *p = malloc(n); remember(p); return p;
}
static void *probe_calloc(size_t n, size_t s) {
    if (refuse()) return NULL;
    void *p = calloc(n,s); remember(p); return p;
}
static void *probe_realloc(void *old, size_t n) {
    if (refuse()) return NULL;
    /* Store its index before realloc; the old pointer is no longer usable
     * after a successful realloc, even when the address is unchanged. */
    size_t index = live_count;
    for (size_t i = 0; i < live_count; ++i) if (live[i] == old) { index = i; break; }
    void *p = realloc(old,n);
    if (p) { if (index < live_count) live[index] = p; else remember(p); }
    return p;
}
static void probe_free(void *p) { forget(p); free(p); }
#define malloc probe_malloc
#define calloc probe_calloc
#define realloc probe_realloc
#define free probe_free
#define eval_native_interrupted probe_interrupted
#include "src/llm_stream.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#undef eval_native_interrupted
bool probe_interrupted(void) { return interrupted; }

static unsigned checks;
static bool tagged(Atom *value, const char *head, const char *reason) {
    return value && value->kind == ATOM_EXPR && value->expr.len == 2 &&
        atom_is_symbol(value->expr.elems[0],head) &&
        (!reason || atom_is_symbol(value->expr.elems[1],reason));
}
static Atom *decode(Arena *out, Atom **args) {
    Atom *value = cetta_llm_response_body(out,args,4);
    assert(live_count == 0); checks++; return value;
}
static void sweep(Arena *out, Atom **args) {
    long failures = 0;
    for (long cut = 0; cut < 1024; ++cut) {
        ArenaMark mark = arena_mark(out);
        allocations = 0; fail_at = cut;
        Atom *value = decode(out,args); fail_at = -1;
        if (tagged(value,"llm:v1:ok",NULL)) {
            assert(failures >= 4); arena_reset(out,mark); break;
        }
        assert(tagged(value,"llm:v1:error","stream-capacity"));
        failures++; arena_reset(out,mark);
        assert(cut != 1023);
    }
    printf("allocation refusals: %ld\n",failures);
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols = &symbols;
    Arena input, out; arena_init_detached(&input); arena_init_detached(&out);
    const char *chat = "data: {\"id\":\"r\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"x\",\"reasoning\":\"a\"},\"finish_reason\":null}]}\n\n"
        "data: {\"id\":\"r\",\"choices\":[{\"index\":0,\"delta\":{\"content\":\"x\",\"reasoning\":\"a\"},\"finish_reason\":\"stop\"}],\"usage\":{\"prompt_tokens\":1}}\n\n"
        "data: [DONE]";
    Atom *args[] = {atom_string(&input,"chat-completions"),atom_bool(&input,true),
        atom_string(&input,chat),atom_int(&input,8388608)};
    Atom *value = decode(&out,args); assert(tagged(value,"llm:v1:ok",NULL));
    assert(strstr(value->expr.elems[1]->ground.sval,"\"content\": \"xx\""));
    assert(strstr(value->expr.elems[1]->ground.sval,"\"reasoning_content\": \"aa\""));
    sweep(&out,args);
    args[2] = atom_string_n(&input,chat,strlen(chat)-strlen("data: [DONE]"));
    assert(tagged(decode(&out,args),"llm:v1:error","stream-incomplete"));
    args[2] = atom_string(&input,chat);
    const char *tool = "data: {\"id\":\"r\",\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,\"id\":\"c\",\"type\":\"function\",\"function\":{\"name\":\"run\",\"arguments\":\"{\\\"x\\\":1}\"}}]},\"finish_reason\":\"tool_calls\"}]}\n\ndata: [DONE]\n\n";
    args[2] = atom_string(&input,tool); assert(tagged(decode(&out,args),"llm:v1:ok",NULL)); sweep(&out,args);
    args[2] = atom_string(&input,chat);
    interrupted = true; assert(tagged(decode(&out,args),"llm:v1:error","stream-interrupted")); interrupted = false;
    args[3] = atom_int(&input,0); assert(tagged(decode(&out,args),"llm:v1:error","stream-size-limit"));
    args[3] = atom_int(&input,8388608);
    args[2] = atom_string_n(&input,"\xff",1); assert(tagged(decode(&out,args),"llm:v1:error","stream-invalid-text"));
    args[2] = atom_string(&input,"data: {}\n\n"); assert(tagged(decode(&out,args),"llm:v1:error",NULL));
    args[0] = atom_string(&input,"anthropic");
    const char *anthropic =
        "data: {\"type\":\"message_start\",\"message\":{\"id\":\"m\",\"role\":\"assistant\",\"content\":[],\"usage\":{\"input_tokens\":1}}}\n\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":{\"type\":\"text\",\"text\":\"prefix\"}}\n\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":{\"type\":\"text_delta\",\"text\":\"tail\"}}\n\n"
        "data: {\"type\":\"content_block_stop\",\"index\":0}\n\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":2}}\n\n"
        "data: {\"type\":\"message_stop\"}\n\n";
    args[2] = atom_string(&input,anthropic); assert(tagged(decode(&out,args),"llm:v1:ok",NULL)); sweep(&out,args);
    size_t event_len = strlen("data: {\"type\":\"ping\"}\n\n"), count = 65537;
    char *many = malloc(event_len * count); assert(many);
    for (size_t i = 0; i < count; ++i) memcpy(many+i*event_len,"data: {\"type\":\"ping\"}\n\n",event_len);
    args[2] = atom_string_n(&input,many,event_len*count); free(many);
    assert(tagged(decode(&out,args),"llm:v1:error","stream-event-limit"));
    assert(tagged(cetta_llm_response_body(&out,NULL,0),"llm:v1:error","stream-invalid-argument")); checks++;
    arena_free(&out); arena_free(&input); symbol_table_free(&symbols);
    printf("PASS: %u native stream controls, no leaked codec allocation or partial result\n",checks);
}
