#include "llm_stream.h"
#include "eval.h"
#include "utf8.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if CETTA_BUILD_WITH_JSON_GSLT
#include "native/json_python_v1.h"
#include "native/json_direct_v1.h"
#include "native/json_value_v1.h"

/* Mutable storage is private to one call. Object order is first insertion;
 * replacing a field changes its value without changing its position. */
typedef struct { bool capacity; } StreamMemory;
typedef struct { char *data; size_t len, cap; StreamMemory *memory; } StreamBytes;
typedef struct StreamObject StreamObject;
typedef enum { STREAM_RAW, STREAM_TEXT, STREAM_OBJECT, STREAM_BLOCKS } StreamValueKind;
typedef struct {
    char *key; size_t key_len;
    StreamValueKind kind;
    StreamBytes raw;
    const void *borrowed;
} StreamField;
struct StreamObject { StreamField *fields; size_t len, cap; size_t *slots, slot_cap; StreamMemory *memory; };
typedef struct {
    StreamObject object;
    StreamBytes pieces[4];
    bool seen[4], closed;
    unsigned type;
} StreamBlock;
typedef struct {
    StreamBytes id, name, arguments;
    bool used;
} StreamTool;
typedef struct {
    StreamMemory memory;
    Arena frame;
    StreamBytes text, reasoning, refusal, identity, usage;
    StreamTool tools[32];
    StreamObject message, anthropic_usage;
    StreamBlock **blocks; size_t block_cap, block_count;
    const char *finish, *error;
    size_t events;
    bool anthropic, started, done, stop_known, usage_valid, reasoning_seen, refusal_seen;
} StreamState;

static bool stream_bytes_add(StreamBytes *b, const char *data, size_t len) {
    if (len > SIZE_MAX - b->len - 1u) { b->memory->capacity = true; return false; }
    size_t need = b->len + len + 1u;
    if (need > b->cap) {
        size_t cap = b->cap ? b->cap : 128u;
        while (cap < need) {
            if (cap > SIZE_MAX / 2u) { cap = need; break; }
            cap *= 2u;
        }
        char *next = realloc(b->data, cap);
        if (!next) { b->memory->capacity = true; return false; }
        b->data = next; b->cap = cap;
    }
    if (len) memcpy(b->data + b->len, data, len);
    b->len += len; b->data[b->len] = 0;
    return true;
}
static bool stream_literal(StreamBytes *b, const char *s) { return stream_bytes_add(b, s, strlen(s)); }
static void stream_bytes_clear(StreamBytes *b) { free(b->data); b->data = NULL; b->len = b->cap = 0u; }
static bool stream_head(Atom *a, const char *name, uint32_t len) {
    return a && a->kind == ATOM_EXPR && a->expr.len == len && atom_is_symbol(a->expr.elems[0], name);
}
static bool stream_string(Atom *a, const char **data, size_t *len) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_STRING) return false;
    *data = a->ground.sval; *len = atom_string_len(a); return true;
}
static bool stream_string_is(Atom *a, const char *s) {
    const char *data; size_t len;
    return stream_string(a, &data, &len) && len == strlen(s) && !memcmp(data, s, len);
}
static Atom *stream_pairs(Atom *a) { return stream_head(a, "JsonObject", 2u) ? a->expr.elems[1] : NULL; }
static Atom *stream_items(Atom *a) { return stream_head(a, "JsonArray", 2u) ? a->expr.elems[1] : NULL; }
static Atom *stream_get(Atom *a, const char *key) {
    Atom *pairs = stream_pairs(a);
    if (!pairs || pairs->kind != ATOM_EXPR) return NULL;
    for (uint32_t i = 0; i < pairs->expr.len; ++i) {
        Atom *pair = pairs->expr.elems[i];
        if (stream_head(pair, "JsonPair", 3u) && stream_string_is(pair->expr.elems[1], key)) return pair->expr.elems[2];
    }
    return NULL;
}
static bool stream_null(Atom *a) { return !a || atom_is_symbol(a, "JsonNull"); }
static bool stream_truth(Atom *a) {
    if (stream_null(a) || atom_is_symbol(a, "False") || atom_is_symbol(a, "false")) return false;
    if (a->kind == ATOM_GROUNDED) {
        if (a->ground.gkind == GV_BOOL) return a->ground.bval;
        if (a->ground.gkind == GV_STRING) return atom_string_len(a) != 0u;
        if (a->ground.gkind == GV_INT) return a->ground.ival != 0;
        if (a->ground.gkind == GV_FLOAT) return a->ground.fval != 0;
    }
    Atom *items = stream_pairs(a); if (!items) items = stream_items(a);
    return !items || items->kind != ATOM_EXPR || items->expr.len != 0u;
}
static bool stream_index(Atom *a, size_t limit, size_t *value) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT || a->ground.ival < 0 ||
        (uint64_t)a->ground.ival >= limit) return false;
    *value = (size_t)a->ground.ival; return true;
}
static bool stream_append_atom(StreamBytes *b, Atom *a) {
    const char *data; size_t len;
    return stream_string(a, &data, &len) && stream_bytes_add(b, data, len);
}
static uint64_t stream_key_hash(const char *key, size_t len) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < len; ++i) { hash ^= (uint8_t)key[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}
static bool stream_rehash(StreamObject *o, size_t cap) {
    if (cap > SIZE_MAX / sizeof(size_t)) { o->memory->capacity = true; return false; }
    size_t *slots = calloc(cap, sizeof(*slots)); if (!slots) { o->memory->capacity = true; return false; }
    for (size_t i = 0; i < o->len; ++i) {
        size_t at = (size_t)stream_key_hash(o->fields[i].key, o->fields[i].key_len) & (cap - 1u);
        while (slots[at]) at = (at + 1u) & (cap - 1u);
        slots[at] = i + 1u;
    }
    free(o->slots); o->slots = slots; o->slot_cap = cap; return true;
}
static StreamField *stream_field(StreamObject *o, const char *key, size_t len) {
    if (!o->slot_cap || o->len + 1u > o->slot_cap / 2u)
        if (!stream_rehash(o, o->slot_cap ? o->slot_cap * 2u : 16u)) return NULL;
    size_t at = (size_t)stream_key_hash(key, len) & (o->slot_cap - 1u);
    while (o->slots[at]) {
        StreamField *f = &o->fields[o->slots[at] - 1u];
        if (f->key_len == len && !memcmp(f->key, key, len)) return f;
        at = (at + 1u) & (o->slot_cap - 1u);
    }
    if (o->len == o->cap) {
        size_t cap = o->cap ? o->cap * 2u : 8u;
        if (cap > SIZE_MAX / sizeof(*o->fields)) { o->memory->capacity = true; return NULL; }
        StreamField *fields = realloc(o->fields, cap * sizeof(*fields));
        if (!fields) { o->memory->capacity = true; return NULL; }
        o->fields = fields; o->cap = cap;
    }
    char *copy = malloc(len + 1u); if (!copy) { o->memory->capacity = true; return NULL; }
    memcpy(copy, key, len); copy[len] = 0;
    StreamField *f = &o->fields[o->len]; *f = (StreamField){.key=copy,.key_len=len,.raw={.memory=o->memory}};
    o->slots[at] = ++o->len; return f;
}
static void stream_object_clear(StreamObject *o) {
    for (size_t i = 0; i < o->len; ++i) { free(o->fields[i].key); stream_bytes_clear(&o->fields[i].raw); }
    free(o->fields); free(o->slots); StreamMemory *memory = o->memory; *o = (StreamObject){.memory=memory};
}
static bool stream_json_bytes(Atom *value, StreamBytes *out) {
    CettaJsonPythonOptionsV1 options = {.ensure_ascii=false,.item_separator=", ",.item_separator_len=2u,.key_separator=": ",.key_separator_len=2u};
    uint8_t *bytes = NULL; size_t len = 0; CettaJsonValueV1Status status; char error[80];
    if (!cetta_json_value_v1_emit_python(cetta_json_direct_v1_number_valid, value, true, &options,
        0u, 0u, 0u, &bytes, &len, &status, error, sizeof(error))) {
        if (status == CETTA_JSON_VALUE_V1_ALLOCATION_FAILURE || status == CETTA_JSON_VALUE_V1_RESOURCE_LIMIT) out->memory->capacity = true;
        return false;
    }
    stream_bytes_clear(out); out->data = (char *)bytes; out->len = len; out->cap = len;
    return true;
}
static bool stream_put_atom(StreamObject *o, const char *key, size_t len, Atom *value) {
    StreamField *f = stream_field(o, key, len); if (!f) return false;
    f->kind = STREAM_RAW; f->borrowed = NULL; return stream_json_bytes(value, &f->raw);
}
static bool stream_copy_object(StreamObject *o, Atom *value) {
    Atom *pairs = stream_pairs(value); if (!pairs || pairs->kind != ATOM_EXPR) return false;
    for (uint32_t i = 0; i < pairs->expr.len; ++i) {
        Atom *pair = pairs->expr.elems[i]; const char *key; size_t len;
        if (!stream_head(pair, "JsonPair", 3u) || !stream_string(pair->expr.elems[1], &key, &len) ||
            !stream_put_atom(o, key, len, pair->expr.elems[2])) return false;
    }
    return true;
}
static bool stream_put_borrowed(StreamObject *o, const char *key, StreamValueKind kind, const void *value) {
    StreamField *f = stream_field(o, key, strlen(key)); if (!f) return false;
    stream_bytes_clear(&f->raw); f->kind = kind; f->borrowed = value; return true;
}
static bool stream_quote(StreamBytes *out, const char *text, size_t len) {
    static const char hex[] = "0123456789abcdef";
    if (!stream_literal(out, "\"")) return false;
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 32u && c != '"' && c != '\\') continue;
        if (!stream_bytes_add(out, text + start, i - start)) return false;
        const char *escaped = c == '"' ? "\\\"" : c == '\\' ? "\\\\" : c == '\b' ? "\\b" :
            c == '\f' ? "\\f" : c == '\n' ? "\\n" : c == '\r' ? "\\r" : c == '\t' ? "\\t" : NULL;
        if (escaped) { if (!stream_literal(out, escaped)) return false; }
        else { char code[] = {'\\','u','0','0',hex[c >> 4u],hex[c & 15u]}; if (!stream_bytes_add(out, code, 6u)) return false; }
        start = i + 1u;
    }
    return stream_bytes_add(out, text + start, len - start) && stream_literal(out, "\"");
}
static bool stream_render_object(StreamBytes *out, const StreamObject *o);
static bool stream_render_blocks(StreamBytes *out, const StreamState *s) {
    if (!stream_literal(out, "[")) return false;
    for (size_t i = 0; i < s->block_count; ++i)
        if ((i && !stream_literal(out, ", ")) || !stream_render_object(out, &s->blocks[i]->object)) return false;
    return stream_literal(out, "]");
}
static bool stream_render_object(StreamBytes *out, const StreamObject *o) {
    if (!stream_literal(out, "{")) return false;
    for (size_t i = 0; i < o->len; ++i) {
        const StreamField *f = &o->fields[i];
        if ((i && !stream_literal(out, ", ")) || !stream_quote(out, f->key, f->key_len) || !stream_literal(out, ": ")) return false;
        if (f->kind == STREAM_RAW) { if (!stream_bytes_add(out, f->raw.data, f->raw.len)) return false; }
        else if (f->kind == STREAM_TEXT) {
            const StreamBytes *b = f->borrowed;
            if (!stream_quote(out, b->data ? b->data : "", b->len)) return false;
        } else if (f->kind == STREAM_OBJECT) { if (!stream_render_object(out, f->borrowed)) return false; }
        else if (!stream_render_blocks(out, f->borrowed)) return false;
    }
    return stream_literal(out, "}");
}
static bool stream_parse(StreamState *s, const char *data, size_t len, Atom **out) {
    CettaJsonPythonV1Error error;
    if (!cetta_json_python_v1_parse(&s->frame, (const uint8_t *)data, len, false, out, &error)) {
        s->error = error.status == CETTA_JSON_PYTHON_V1_ALLOCATION_FAILURE ? "stream-capacity" : "stream-invalid-json";
        return false;
    }
    return true;
}
static bool stream_chat_event(StreamState *s, const char *kind, size_t kind_len, const char *data, size_t len) {
    if (s->done) return false;
    if (len == 6u && !memcmp(data, "[DONE]", 6u)) { s->done = true; return true; }
    Atom *value;
    if (!stream_parse(s, data, len, &value) || !stream_pairs(value) ||
        (kind_len == 5u && !memcmp(kind, "error", 5u)) || stream_get(value, "error")) return false;
    Atom *identity = stream_get(value, "id"); const char *id; size_t id_len;
    if (!stream_string(identity, &id, &id_len) || !id_len) return false;
    if (!s->identity.len) { if (!stream_bytes_add(&s->identity, id, id_len)) return false; }
    else if (s->identity.len != id_len || memcmp(s->identity.data, id, id_len)) return false;
    Atom *usage = stream_get(value, "usage");
    if (!stream_null(usage) && (!stream_pairs(usage) || !stream_json_bytes(usage, &s->usage))) return false;
    Atom *choices = stream_items(stream_get(value, "choices"));
    if (!choices || choices->kind != ATOM_EXPR || choices->expr.len > 1u) return false;
    if (!choices->expr.len) return true;
    Atom *choice = choices->expr.elems[0]; size_t index;
    if (!stream_pairs(choice) || !stream_index(stream_get(choice, "index"), 1u, &index) || s->finish) return false;
    Atom *delta = stream_get(choice, "delta");
    if (!stream_pairs(delta)) return false;
    Atom *role = stream_get(delta, "role");
    if (!stream_null(role) && !stream_string_is(role, "assistant")) return false;
    const char *fields[] = {"content", "reasoning_content", "reasoning", "refusal"};
    StreamBytes *destinations[] = {&s->text,&s->reasoning,&s->reasoning,&s->refusal};
    for (size_t i = 0; i < 4u; ++i) {
        Atom *part = stream_get(delta, fields[i]);
        if (!stream_null(part)) {
            if (!stream_append_atom(destinations[i], part)) return false;
            if (i == 1u || i == 2u) s->reasoning_seen = true;
            if (i == 3u) s->refusal_seen = true;
        }
    }
    Atom *calls_value = stream_get(delta, "tool_calls");
    if (stream_truth(calls_value)) {
        Atom *calls = stream_items(calls_value);
        if (!calls || calls->kind != ATOM_EXPR) return false;
        for (uint32_t i = 0; i < calls->expr.len; ++i) {
            Atom *call = calls->expr.elems[i];
            if (!stream_pairs(call) || !stream_index(stream_get(call, "index"), 32u, &index)) return false;
            Atom *type = stream_get(call, "type");
            if (!stream_null(type) && !stream_string_is(type, "function")) return false;
            StreamTool *tool = &s->tools[index]; tool->used = true;
            Atom *function = stream_get(call, "function");
            if (function && !stream_pairs(function)) return false;
            Atom *pieces[] = {stream_get(call,"id"),stream_get(function,"name"),stream_get(function,"arguments")};
            StreamBytes *buffers[] = {&tool->id,&tool->name,&tool->arguments};
            for (size_t j = 0; j < 3u; ++j) if (!stream_null(pieces[j]) && !stream_append_atom(buffers[j], pieces[j])) return false;
        }
    }
    Atom *reason = stream_get(choice, "finish_reason");
    if (!stream_null(reason)) {
        const char *allowed[] = {"stop","tool_calls","length","content_filter","refusal"};
        for (size_t i = 0; i < 5u; ++i) if (stream_string_is(reason, allowed[i])) { s->finish = allowed[i]; break; }
        if (!s->finish) return false;
    }
    return true;
}
static bool stream_usage_set(StreamState *s, Atom *value) {
    stream_object_clear(&s->anthropic_usage);
    s->usage_valid = !value || stream_pairs(value);
    return !value || !s->usage_valid || stream_copy_object(&s->anthropic_usage, value);
}
static bool stream_anthropic_event(StreamState *s, const char *kind, size_t kind_len, const char *data, size_t len) {
    if (s->done) return false;
    Atom *value;
    if (!stream_parse(s, data, len, &value) || !stream_pairs(value)) return false;
    Atom *type = stream_get(value, "type"); const char *type_text; size_t type_len;
    bool named = stream_string(type, &type_text, &type_len);
    if (kind_len && (!named || type_len != kind_len || memcmp(kind, type_text, kind_len))) return false;
    if (stream_string_is(type, "error")) return false;
    if (stream_string_is(type, "ping")) return true;
    if (stream_string_is(type, "message_start")) {
        if (s->started) return false;
        Atom *message = stream_get(value, "message");
        if (!stream_pairs(message) || !stream_truth(stream_get(message,"id")) ||
            !stream_string_is(stream_get(message,"role"),"assistant") || stream_truth(stream_get(message,"content"))) return false;
        s->started = true;
        return stream_copy_object(&s->message, message) && stream_usage_set(s, stream_get(message,"usage"));
    }
    if (stream_string_is(type, "content_block_start")) {
        size_t index; Atom *block_value = stream_get(value, "content_block");
        if (!s->started || !stream_index(stream_get(value,"index"),65536u,&index) || !stream_pairs(block_value)) return false;
        if (index >= s->block_cap) {
            size_t cap = s->block_cap ? s->block_cap : 8u;
            while (cap <= index) cap *= 2u;
            StreamBlock **blocks = realloc(s->blocks, cap * sizeof(*blocks));
            if (!blocks) { s->memory.capacity = true; return false; }
            memset(blocks + s->block_cap, 0, (cap - s->block_cap) * sizeof(*blocks));
            s->blocks = blocks; s->block_cap = cap;
        }
        if (s->blocks[index]) return false;
        StreamBlock *block = calloc(1u, sizeof(*block)); if (!block) { s->memory.capacity = true; return false; }
        block->object.memory = &s->memory;
        for (size_t j = 0; j < 4u; ++j) block->pieces[j].memory = &s->memory;
        s->blocks[index] = block; s->block_count++;
        Atom *block_type = stream_get(block_value,"type");
        block->type = stream_string_is(block_type,"text") ? 1u : stream_string_is(block_type,"thinking") ? 2u : stream_string_is(block_type,"tool_use") ? 3u : 0u;
        return stream_copy_object(&block->object, block_value);
    }
    if (stream_string_is(type,"content_block_delta") || stream_string_is(type,"content_block_stop")) {
        size_t index;
        if (!stream_index(stream_get(value,"index"),65536u,&index) || index >= s->block_cap || !s->blocks[index] || s->blocks[index]->closed) return false;
        StreamBlock *block = s->blocks[index];
        if (stream_string_is(type,"content_block_stop")) {
            if (block->seen[3]) {
                Atom *input;
                if (!stream_parse(s, block->pieces[3].data ? block->pieces[3].data : "", block->pieces[3].len, &input) || !stream_pairs(input) ||
                    !stream_put_atom(&block->object,"input",5u,input)) return false;
            }
            block->closed = true; return true;
        }
        Atom *delta = stream_get(value,"delta"); if (!stream_pairs(delta)) return false;
        Atom *delta_type = stream_get(delta,"type");
        const char *types[] = {"text_delta","thinking_delta","signature_delta","input_json_delta"};
        const char *targets[] = {"text","thinking","signature","input"};
        const char *sources[] = {"text","thinking","signature","partial_json"};
        unsigned expected[] = {1u,2u,2u,3u};
        for (size_t i = 0; i < 4u; ++i) if (stream_string_is(delta_type,types[i])) {
            Atom *piece = stream_get(delta,sources[i]); const char *bytes; size_t count;
            if (block->type != expected[i] || !stream_string(piece,&bytes,&count)) return false;
            if (!block->seen[i]) {
                if (i != 3u) {
                    StreamField *field = stream_field(&block->object,targets[i],strlen(targets[i]));
                    if (!field) return false;
                    if (field->raw.len) {
                        Atom *initial;
                        if (!stream_parse(s,field->raw.data,field->raw.len,&initial) || !stream_append_atom(&block->pieces[i],initial)) return false;
                    }
                    if (!stream_put_borrowed(&block->object,targets[i],STREAM_TEXT,&block->pieces[i])) return false;
                }
                block->seen[i] = true;
            }
            return stream_bytes_add(&block->pieces[i],bytes,count);
        }
        return !stream_pairs(delta_type) && !stream_items(delta_type);
    }
    if (stream_string_is(type,"message_delta")) {
        Atom *delta = stream_get(value,"delta");
        Atom *usage = stream_get(value,"usage");
        if (!s->started || !stream_pairs(delta) || (usage && !stream_pairs(usage))) return false;
        Atom *delta_usage = stream_get(delta,"usage");
        if (delta_usage && !stream_usage_set(s,delta_usage)) return false;
        if (!s->usage_valid || !stream_copy_object(&s->message,delta)) return false;
        Atom *stop = stream_get(delta,"stop_reason"); if (stop) s->stop_known = !stream_null(stop);
        if (usage && !stream_copy_object(&s->anthropic_usage,usage)) return false;
        return stream_put_borrowed(&s->message,"usage",STREAM_OBJECT,&s->anthropic_usage);
    }
    if (stream_string_is(type,"message_stop")) {
        if (!s->started || !s->stop_known) return false;
        for (size_t i = 0; i < s->block_count; ++i) if (!s->blocks[i] || !s->blocks[i]->closed) return false;
        s->done = true; return true;
    }
    return true;
}
static bool stream_event(StreamState *s, const StreamBytes *kind, const StreamBytes *data) {
    if (eval_native_interrupted()) { s->error = "stream-interrupted"; return false; }
    if (s->events++ >= 65536u) { s->error = "stream-event-limit"; return false; }
    bool ok = s->anthropic ? stream_anthropic_event(s,kind->data,kind->len,data->data,data->len)
                          : stream_chat_event(s,kind->data,kind->len,data->data,data->len);
    return ok;
}
static bool stream_frames(StreamState *s, const char *body, size_t len) {
    StreamBytes data = {.memory=&s->memory}, kind = {.memory=&s->memory}; size_t lines = 0, at = 0; bool ok = true;
    if (len >= 3u && !memcmp(body,"\xef\xbb\xbf",3u)) at = 3u;
    ArenaMark mark = arena_mark(&s->frame);
    while (at < len && ok) {
        size_t start = at;
        while (at < len && body[at] != '\r' && body[at] != '\n') ++at;
        size_t count = at - start;
        if (at < len) { char ending = body[at++]; if (ending == '\r' && at < len && body[at] == '\n') ++at; }
        if (!count) {
            if (lines) { ok = stream_event(s,&kind,&data); arena_reset(&s->frame,mark); }
            data.len = kind.len = lines = 0u;
        } else if (body[start] != ':') {
            size_t colon = 0u; while (colon < count && body[start+colon] != ':') ++colon;
            size_t value = colon < count ? colon + 1u : count;
            if (value < count && body[start+value] == ' ') ++value;
            if (colon == 4u && !memcmp(body+start,"data",4u)) {
                if (lines && !stream_literal(&data,"\n")) { ok = false; break; }
                ok = stream_bytes_add(&data,body+start+value,count-value); lines++;
            } else if (colon == 5u && !memcmp(body+start,"event",5u)) {
                kind.len = 0u; ok = stream_bytes_add(&kind,body+start+value,count-value);
            }
        }
    }
    /* Match the buffered line contract: a terminated last line contributes
     * split's final empty line. An unterminated JSON event still refuses. */
    if (ok && lines && len && (body[len-1u] == '\r' || body[len-1u] == '\n')) {
        ok = stream_event(s,&kind,&data); lines = 0u;
    }
    if (ok && lines) {
        if (lines == 1u && !kind.len && data.len == 6u && !memcmp(data.data,"[DONE]",6u)) ok = stream_event(s,&kind,&data);
        else { s->error = "stream-truncated"; ok = false; }
    }
    stream_bytes_clear(&data); stream_bytes_clear(&kind); return ok;
}
static bool stream_python_space(uint32_t cp) {
    return (cp >= 9u && cp <= 13u) || (cp >= 28u && cp <= 32u) || cp == 0x85u || cp == 0xa0u ||
        cp == 0x1680u || (cp >= 0x2000u && cp <= 0x200au) || cp == 0x2028u || cp == 0x2029u ||
        cp == 0x202fu || cp == 0x205fu || cp == 0x3000u;
}
static bool stream_render_chat(StreamState *s, StreamBytes *out) {
    if (!s->done || !s->finish || !s->identity.len) return false;
    size_t tools = 0u;
    for (size_t i = 0; i < 32u; ++i) if (s->tools[i].used) {
        StreamTool *tool = &s->tools[i]; Atom *arguments;
        if (i != tools || !tool->id.len || !tool->name.len || strcmp(s->finish,"tool_calls") ||
            !stream_parse(s,tool->arguments.data ? tool->arguments.data : "",tool->arguments.len,&arguments) || !stream_pairs(arguments)) return false;
        for (size_t j = 0; j < i; ++j) if (s->tools[j].id.len == tool->id.len && !memcmp(s->tools[j].id.data,tool->id.data,tool->id.len)) return false;
        tools++;
    }
    if (!tools && !strcmp(s->finish,"tool_calls")) return false;
    if (!stream_literal(out,"{\"id\": ") || !stream_quote(out,s->identity.data,s->identity.len) ||
        !stream_literal(out,", \"choices\": [{\"index\": 0, \"message\": {\"role\": \"assistant\", \"content\": ") ||
        !stream_quote(out,s->text.data ? s->text.data : "",s->text.len)) return false;
    if (s->reasoning_seen && (!stream_literal(out,", \"reasoning_content\": ") || !stream_quote(out,s->reasoning.data ? s->reasoning.data : "",s->reasoning.len))) return false;
    if (s->refusal_seen && (!stream_literal(out,", \"refusal\": ") || !stream_quote(out,s->refusal.data ? s->refusal.data : "",s->refusal.len))) return false;
    if (tools) {
        if (!stream_literal(out,", \"tool_calls\": [")) return false;
        for (size_t i = 0; i < tools; ++i) {
            StreamTool *t = &s->tools[i];
            if ((i && !stream_literal(out,", ")) || !stream_literal(out,"{\"id\": ") || !stream_quote(out,t->id.data,t->id.len) ||
                !stream_literal(out,", \"type\": \"function\", \"function\": {\"name\": ") || !stream_quote(out,t->name.data,t->name.len) ||
                !stream_literal(out,", \"arguments\": ") || !stream_quote(out,t->arguments.data,t->arguments.len) || !stream_literal(out,"}}")) return false;
        }
        if (!stream_literal(out,"]")) return false;
    }
    return stream_literal(out,"}, \"finish_reason\": ") && stream_quote(out,s->finish,strlen(s->finish)) &&
        stream_literal(out,"}], \"usage\": ") && stream_bytes_add(out,s->usage.data ? s->usage.data : "{}",s->usage.data ? s->usage.len : 2u) && stream_literal(out,"}");
}
static void stream_state_clear(StreamState *s) {
    stream_bytes_clear(&s->text); stream_bytes_clear(&s->reasoning); stream_bytes_clear(&s->refusal);
    stream_bytes_clear(&s->identity); stream_bytes_clear(&s->usage);
    for (size_t i = 0; i < 32u; ++i) { stream_bytes_clear(&s->tools[i].id); stream_bytes_clear(&s->tools[i].name); stream_bytes_clear(&s->tools[i].arguments); }
    for (size_t i = 0; i < s->block_cap; ++i) if (s->blocks[i]) {
        StreamBlock *b = s->blocks[i]; stream_object_clear(&b->object);
        for (size_t j = 0; j < 4u; ++j) stream_bytes_clear(&b->pieces[j]);
        free(b);
    }
    free(s->blocks); stream_object_clear(&s->message); stream_object_clear(&s->anthropic_usage); arena_free(&s->frame);
}
#endif

Atom *cetta_llm_response_body(Arena *arena, Atom **args, uint32_t nargs) {
    const char *error = "stream-invalid-argument";
#if CETTA_BUILD_WITH_JSON_GSLT
    const char *protocol, *body; size_t protocol_len, len; bool enabled;
    if (nargs != 4u || !args || !args[1] || !args[3] ||
        !stream_string(args[0],&protocol,&protocol_len) || !stream_string(args[2],&body,&len) ||
        args[3]->kind != ATOM_GROUNDED || args[3]->ground.gkind != GV_INT || args[3]->ground.ival < 0) goto refused;
    if ((uint64_t)len > (uint64_t)args[3]->ground.ival) { error = "stream-size-limit"; goto refused; }
    if (args[1]->kind == ATOM_GROUNDED && args[1]->ground.gkind == GV_BOOL) enabled = args[1]->ground.bval;
    else if (atom_is_symbol(args[1],"True") || atom_is_symbol(args[1],"true")) enabled = true;
    else if (atom_is_symbol(args[1],"False") || atom_is_symbol(args[1],"false")) enabled = false;
    else goto refused;
    bool anthropic = protocol_len == 9u && !memcmp(protocol,"anthropic",9u);
    if (!anthropic && !(protocol_len == 16u && !memcmp(protocol,"chat-completions",16u))) {
        error = "stream-unsupported-protocol"; goto refused;
    }
    size_t first = len;
    for (size_t at = 0; at < len;) {
        uint32_t cp, width;
        if (!cetta_utf8_decode_one((const uint8_t *)body,len,at,&cp,&width)) { error = "stream-invalid-text"; goto refused; }
        if (first == len && !stream_python_space(cp)) first = at;
        at += width;
    }
    if (!enabled || (first < len && body[first]=='{')) return atom_expr2(arena,atom_symbol(arena,"llm:v1:ok"),args[2]);
    StreamState state = {.anthropic=anthropic,.usage_valid=true}; arena_init_detached(&state.frame);
    state.message.memory = state.anthropic_usage.memory = &state.memory;
    StreamBytes *buffers[] = {&state.text,&state.reasoning,&state.refusal,&state.identity,&state.usage};
    for (size_t i = 0; i < 5u; ++i) buffers[i]->memory = &state.memory;
    for (size_t i = 0; i < 32u; ++i) state.tools[i].id.memory = state.tools[i].name.memory = state.tools[i].arguments.memory = &state.memory;
    StreamBytes out = {.memory=&state.memory};
    bool ok = stream_frames(&state,body,len);
    if (ok && state.anthropic) {
        ok = state.done && stream_put_borrowed(&state.message,"content",STREAM_BLOCKS,&state) && stream_render_object(&out,&state.message);
    } else if (ok) ok = stream_render_chat(&state,&out);
    /* Python-compatible JSON accepts escaped lone surrogates. They cannot
     * cross this UTF-8 body interface; refuse before publishing any bytes. */
    for (size_t at = 0; ok && at < out.len;) {
        uint32_t cp, width;
        if (!cetta_utf8_decode_one((const uint8_t *)out.data,out.len,at,&cp,&width)) {
            state.error = "stream-invalid-text"; ok = false; break;
        }
        at += width;
    }
    error = state.memory.capacity ? "stream-capacity" : state.error ? state.error : "stream-incomplete";
    Atom *result = ok ? atom_string_n(arena,out.data ? out.data : "",out.len) : NULL;
    stream_bytes_clear(&out); stream_state_clear(&state);
    if (ok && !result) return NULL;
    if (ok) return atom_expr2(arena,atom_symbol(arena,"llm:v1:ok"),result);
refused:
#else
    (void)args; (void)nargs; error = "stream-json-unavailable";
#endif
    return atom_expr2(arena,atom_symbol(arena,"llm:v1:error"),atom_symbol(arena,error));
}
