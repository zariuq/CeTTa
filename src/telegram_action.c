#define _POSIX_C_SOURCE 200809L
#include "telegram_action.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned method;
    int64_t chat, thread, message;
    const char *text, *format, *callback;
    const Atom *keyboard;
    size_t bytes, keyboard_bytes, buttons;
} Action;
static bool integer(const Atom *a, int64_t *n) {
    if (!a || a->kind!=ATOM_GROUNDED || a->ground.gkind!=GV_INT) return false;
    *n=a->ground.ival; return true;
}
static const char *string(const Atom *a) {
    return a && a->kind==ATOM_GROUNDED && a->ground.gkind==GV_STRING?a->ground.sval:NULL;
}
/* Valid UTF-8 of min..max Unicode scalars and at most bytes_max bytes;
 * control characters only where controls allows them. */
static bool utf8(const char *text, size_t min, size_t max, size_t bytes_max, bool controls, size_t *bytes) {
    if (!text) return false;
    size_t n=strnlen(text,bytes_max+1), scalars=0;
    if (n>bytes_max) return false;
    const unsigned char *s=(const unsigned char *)text;
    for (size_t i=0;i<n;) {
        if (++scalars>max) return false;
        unsigned c=s[i++], more, min_value;
        if (c<128) { if (c<32 && !controls) return false; continue; }
        if (c>=0xc2 && c<=0xdf) { more=1; min_value=0x80; c&=31; }
        else if (c>=0xe0 && c<=0xef) { more=2; min_value=0x800; c&=15; }
        else if (c>=0xf0 && c<=0xf4) { more=3; min_value=0x10000; c&=7; }
        else return false;
        if (more>n-i) return false;
        while (more--) { unsigned d=s[i++]; if ((d&0xc0)!=0x80) return false; c=(c<<6)|(d&63); }
        if (c<min_value || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return false;
    }
    if (scalars<min) return false;
    *bytes=n; return true;
}
static bool message_text(const char *text, size_t *bytes) { return utf8(text,1,4096,16384,true,bytes); }
/* (telegram:keyboard ROWS): 1..100 rows of 1..8 (telegram:button LABEL DATA),
 * at most 100 buttons. A label is 1..64 scalars without control characters;
 * callback data is 1..64 bytes of UTF-8, as Telegram allows. */
static bool keyboard(const Atom *k, Action *a) {
    if (!k || k->kind!=ATOM_EXPR || k->expr.len!=2 || !atom_is_symbol(k->expr.elems[0],"telegram:keyboard")) return false;
    const Atom *rows=k->expr.elems[1];
    if (rows->kind!=ATOM_EXPR || !rows->expr.len || rows->expr.len>100) return false;
    size_t buttons=0, bytes=0;
    for (CettaExprIndex i=0;i<rows->expr.len;++i) {
        const Atom *row=rows->expr.elems[i];
        if (row->kind!=ATOM_EXPR || !row->expr.len || row->expr.len>8) return false;
        for (CettaExprIndex j=0;j<row->expr.len;++j) {
            const Atom *b=row->expr.elems[j]; size_t label, data;
            if (b->kind!=ATOM_EXPR || b->expr.len!=3 || !atom_is_symbol(b->expr.elems[0],"telegram:button") ||
                !utf8(string(b->expr.elems[1]),1,64,256,false,&label) ||
                !utf8(string(b->expr.elems[2]),1,64,64,false,&data) || ++buttons>100) return false;
            bytes+=label+data;
        }
    }
    a->keyboard=rows; a->keyboard_bytes=bytes; a->buttons=buttons; return true;
}
/* A callback query ID as Telegram issues it: 1..64 letters, digits, - or _. */
static bool callback_id(const char *id) {
    size_t n=id?strnlen(id,65):0;
    if (!n || n>64) return false;
    for (size_t i=0;i<n;++i) if (!((id[i]>='a' && id[i]<='z') || (id[i]>='A' && id[i]<='Z') ||
        (id[i]>='0' && id[i]<='9') || id[i]=='-' || id[i]=='_')) return false;
    return true;
}
static bool decode(const CettaTelegramActionPolicy *policy, const Atom *p, Action *a) {
    if (!policy || !policy->chats || !policy->chat_count || policy->chat_count>128 ||
        (policy->methods & ~15u) || !p || p->kind!=ATOM_EXPR || p->expr.len<4) return false;
    Atom **v=p->expr.elems; int64_t version;
    if (!integer(v[1],&version) || version!=1 || !integer(v[2],&a->chat) || !a->chat) return false;
    bool allowed=false;
    for (size_t i=0;i<policy->chat_count;++i) if (policy->chats[i]==a->chat) allowed=true;
    if (!allowed) return false;
    if (atom_is_symbol(v[0],"telegram:send-text") && (p->expr.len==7 || p->expr.len==8)) {
        a->method=TELEGRAM_SEND_TEXT;
        if (!integer(v[3],&a->thread) || a->thread<0 || !integer(v[4],&a->message) || a->message<0) return false;
        a->text=string(v[5]); a->format=string(v[6]);
        if (p->expr.len==8 && !keyboard(v[7],a)) return false;
    } else if (atom_is_symbol(v[0],"telegram:edit-text") && (p->expr.len==6 || p->expr.len==7)) {
        a->method=TELEGRAM_EDIT_TEXT;
        if (!integer(v[3],&a->message) || a->message<=0) return false;
        a->text=string(v[4]); a->format=string(v[5]);
        if (p->expr.len==7 && !keyboard(v[6],a)) return false;
    } else if (atom_is_symbol(v[0],"telegram:delete-message") && p->expr.len==4) {
        a->method=TELEGRAM_DELETE_MESSAGE;
        if (!integer(v[3],&a->message) || a->message<=0) return false;
    } else if (atom_is_symbol(v[0],"telegram:answer-callback") && p->expr.len==5) {
        /* The chat is the one whose message carried the button; it is only
         * checked against the policy, since Telegram names the query alone. */
        a->method=TELEGRAM_ANSWER_CALLBACK;
        a->callback=string(v[3]); a->text=string(v[4]);
        if (!callback_id(a->callback) || !utf8(a->text,0,200,800,true,&a->bytes)) return false;
    } else return false;
    if (!(policy->methods&a->method)) return false;
    return a->method==TELEGRAM_DELETE_MESSAGE || a->method==TELEGRAM_ANSWER_CALLBACK ||
        (a->format && (!strcmp(a->format,"plain") || !strcmp(a->format,"HTML") || !strcmp(a->format,"MarkdownV2")) &&
         message_text(a->text,&a->bytes));
}
bool cetta_telegram_action_validate(void *context, const Atom *payload, const Atom *reply) {
    (void)reply; Action a={0}; return decode(context,payload,&a);
}
/* Fixed keys/numeric fields plus one escaped string: no JSON parser or
 * evaluator is involved, and user bytes cannot introduce another field. */
static char *literal(char *out, const char *text) {
    size_t n=strlen(text); memcpy(out,text,n); return out+n;
}
static char *quoted(char *out, const char *text) {
    static const char hex[]="0123456789abcdef";
    *out++='"';
    for (const unsigned char *s=(const unsigned char *)text; *s; ++s) {
        if (*s=='"' || *s=='\\') { *out++='\\'; *out++=(char)*s; }
        else if (*s<32) {
            memcpy(out,"\\u00",4); out+=4; *out++=hex[*s>>4]; *out++=hex[*s&15];
        } else *out++=(char)*s;
    }
    *out++='"'; return out;
}
bool cetta_telegram_action_plan(void *context, Arena *arena, const Atom *payload,
                                const Atom *reply, CettaTelegramPlan *out) {
    (void)reply;
    if (!out) return false;
    *out=(CettaTelegramPlan){0}; Action a={0};
    if (!arena || !decode(context,payload,&a)) return false;
    /* Worst-case control escaping plus fixed keys, three int64 fields, a
     * validated format name and fixed keys per button. All sizes are bounded
     * before allocation. */
    char *body=arena_alloc(arena,(a.bytes+a.keyboard_bytes)*6+a.buttons*48+512), *end=body;
    if (a.method==TELEGRAM_ANSWER_CALLBACK) {
        end=quoted(literal(end,"{\"callback_query_id\":"),a.callback);
        if (a.bytes) { end=quoted(literal(end,",\"text\":"),a.text); }
        *end++='}'; *end=0;
        *out=(CettaTelegramPlan){"answerCallbackQuery","application/json",body,(size_t)(end-body)};
        return true;
    }
    end+=sprintf(end,"{\"chat_id\":%" PRId64,a.chat);
    const char *method=a.method==TELEGRAM_SEND_TEXT?"sendMessage":
        a.method==TELEGRAM_EDIT_TEXT?"editMessageText":"deleteMessage";
    if (a.method==TELEGRAM_SEND_TEXT) {
        if (a.thread) end+=sprintf(end,",\"message_thread_id\":%" PRId64,a.thread);
        if (a.message) end+=sprintf(end,",\"reply_parameters\":{\"message_id\":%" PRId64 "}",a.message);
    } else end+=sprintf(end,",\"message_id\":%" PRId64,a.message);
    if (a.text) {
        end=quoted(literal(end,",\"text\":"),a.text);
        if (strcmp(a.format,"plain")) end+=sprintf(end,",\"parse_mode\":\"%s\"",a.format);
    }
    if (a.keyboard) {
        end=literal(end,",\"reply_markup\":{\"inline_keyboard\":[");
        for (CettaExprIndex i=0;i<a.keyboard->expr.len;++i) {
            const Atom *row=a.keyboard->expr.elems[i];
            if (i) *end++=',';
            *end++='[';
            for (CettaExprIndex j=0;j<row->expr.len;++j) {
                const Atom *b=row->expr.elems[j];
                if (j) *end++=',';
                end=quoted(literal(end,"{\"text\":"),string(b->expr.elems[1]));
                end=quoted(literal(end,",\"callback_data\":"),string(b->expr.elems[2]));
                *end++='}';
            }
            *end++=']';
        }
        end=literal(end,"]}");
    }
    *end++='}'; *end=0;
    *out=(CettaTelegramPlan){method,"application/json",body,(size_t)(end-body)};
    return true;
}
