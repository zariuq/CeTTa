/* tcalc_syntax.c - reads .tcalc programs (the syntax is in tcalc_syntax.h). */

#include "tcalc_syntax.h"
#include "tcalc_axes_names.generated.h"
#include "tcalc_lists.generated.h"
#include "../../src/runner.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* ---- Spellings (declared once, in tcalc_axes.data) ----------------------- */

const char *tc_step_name(TcStep s) { return s < TC_STEP_COUNT ? tc_step_spellings[s] : "?"; }
const char *tc_ask_name(TcAsk a) { return a < TC_ASK_COUNT ? tc_ask_spellings[a] : "?"; }
const char *tc_store_name(TcStore s) { return s < TC_STORE_COUNT ? tc_store_spellings[s] : "?"; }
const char *tc_matches_name(TcMatches m) {
    return m < TC_MATCHES_COUNT ? tc_matches_spellings[m] : "?";
}
const char *tc_backend_name(TcBackend b) {
    return b < TC_BACKEND_COUNT ? tc_backend_spellings[b] : "?";
}
const char *tc_observe_name(TcObserve o) {
    return o < TC_OBSERVE_COUNT ? tc_observe_spellings[o] : "?";
}

static bool spelled(const char *word, const char *name, size_t length) {
    return strlen(word) == length && memcmp(word, name, length) == 0;
}

bool tc_step_from_name(const char *name, size_t length, TcStep *out) {
    for (int i = 0; i < TC_STEP_COUNT; i++)
        if (spelled(tc_step_spellings[i], name, length)) { *out = (TcStep)i; return true; }
    return false;
}

bool tc_ask_from_name(const char *name, size_t length, TcAsk *out) {
    for (int i = 0; i < TC_ASK_COUNT; i++)
        if (spelled(tc_ask_spellings[i], name, length)) { *out = (TcAsk)i; return true; }
    return false;
}

bool tc_store_from_name(const char *name, size_t length, TcStore *out) {
    for (int i = 0; i < TC_STORE_COUNT; i++)
        if (spelled(tc_store_spellings[i], name, length)) { *out = (TcStore)i; return true; }
    return false;
}

bool tc_matches_from_name(const char *name, size_t length, TcMatches *out) {
    for (int i = 0; i < TC_MATCHES_COUNT; i++)
        if (spelled(tc_matches_spellings[i], name, length)) { *out = (TcMatches)i; return true; }
    return false;
}

bool tc_backend_from_name(const char *name, size_t length, TcBackend *out) {
    for (int i = 0; i < TC_BACKEND_COUNT; i++)
        if (spelled(tc_backend_spellings[i], name, length)) { *out = (TcBackend)i; return true; }
    return false;
}

bool tc_observe_from_name(const char *name, size_t length, TcObserve *out) {
    for (int i = 0; i < TC_OBSERVE_COUNT; i++)
        if (spelled(tc_observe_spellings[i], name, length)) { *out = (TcObserve)i; return true; }
    return false;
}

/* ---- S-expressions ---------------------------------------------------------
 * The text is read once into a tree of forms; a form is a word (symbol,
 * integer, string, variable) or a list of forms. */

typedef enum { F_SYMBOL, F_INTEGER, F_STRING, F_VARIABLE, F_LIST } FormKind;

typedef struct {
    FormKind kind;
    size_t line, column;
    const char *text; size_t length;   /* symbol, variable: its bytes in the text  */
    int64_t value;                     /* integer: the value; string: the offset of
                                          its unescaped bytes in the reader's bytes */
    uint32_t first, count;             /* list: its items in the item table        */
} Form;

typedef struct {
    const char *text; size_t length, at, line, column;
    Form *form; size_t forms, form_cap;
    uint32_t *item; size_t items, item_cap;
    char *bytes; size_t bytes_len, bytes_cap;   /* unescaped string contents */
    TcSyntaxError *error;
} Reader;

/* Records the first error met; later ones follow from it. */
static bool fail(Reader *r, size_t line, size_t column, const char *message) {
    if (r->error->message[0] == 0) {
        r->error->line = line;
        r->error->column = column;
        snprintf(r->error->message, sizeof r->error->message, "%s", message);
    }
    return false;
}

/* Memory ran out: no place in the text is to blame (line 0). */
static bool no_memory(Reader *r) { return fail(r, 0, 0, "out of memory"); }

static int peek(const Reader *r) { return r->at < r->length ? (unsigned char)r->text[r->at] : -1; }

static void advance(Reader *r) {
    if (r->text[r->at] == '\n') { r->line++; r->column = 1; } else r->column++;
    r->at++;
}

static void skip_space(Reader *r) {
    for (int c; (c = peek(r)) != -1;) {
        if (c == ';') { while (peek(r) != -1 && peek(r) != '\n') advance(r); }
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') advance(r);
        else return;
    }
}

static bool is_delimiter(int c) {
    return c == -1 || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' || c == ')' ||
           c == ';' || c == '"';
}

/* Reads one form at the current position; its index in *out. */
static bool read_form(Reader *r, uint32_t *out) {
    skip_space(r);
    Form f = {.line = r->line, .column = r->column};
    int c = peek(r);
    if (c == -1) return fail(r, f.line, f.column, "a form was expected, the text ended");
    if (c == ')') return fail(r, f.line, f.column, "a closing parenthesis with nothing open");
    if (c == '(') {
        advance(r);
        uint32_t *mine = NULL; size_t n = 0, cap = 0;
        for (;;) {
            skip_space(r);
            if (peek(r) == -1) { free(mine); return fail(r, f.line, f.column, "this list is never closed"); }
            if (peek(r) == ')') { advance(r); break; }
            uint32_t child;
            if (!read_form(r, &child) || !tc_push_u32(realloc, &mine, &n, &cap, child)) {
                free(mine);
                return no_memory(r);
            }
        }
        f.kind = F_LIST;
        f.first = (uint32_t)r->items;
        f.count = (uint32_t)n;
        for (size_t i = 0; i < n; i++)
            if (!tc_push_u32(realloc, &r->item, &r->items, &r->item_cap, mine[i])) {
                free(mine);
                return no_memory(r);
            }
        free(mine);
    } else if (c == '"') {
        advance(r);
        size_t start = r->bytes_len;
        for (;;) {
            int d = peek(r);
            if (d == -1) return fail(r, f.line, f.column, "this string is never closed");
            advance(r);
            if (d == '"') break;
            if (d == '\\') {
                int e = peek(r);
                if (e == -1) return fail(r, f.line, f.column, "this string is never closed");
                advance(r);
                d = e == 'n' ? '\n' : e == 't' ? '\t' : e;
            }
            if (!tc_push_char(realloc, &r->bytes, &r->bytes_len, &r->bytes_cap, (char)d))
                return no_memory(r);
        }
        f.kind = F_STRING;
        f.value = (int64_t)start;
        f.length = r->bytes_len - start;
    } else {
        size_t start = r->at;
        while (!is_delimiter(peek(r))) advance(r);
        f.text = r->text + start;
        f.length = r->at - start;
        f.kind = f.text[0] == '$' ? F_VARIABLE : F_SYMBOL;
        if (f.kind == F_SYMBOL) {   /* an integer: an optional sign and digits */
            size_t i = (f.text[0] == '-' && f.length > 1) ? 1 : 0;
            bool digits = i < f.length;
            for (size_t k = i; k < f.length; k++) digits = digits && f.text[k] >= '0' && f.text[k] <= '9';
            if (digits) {
                char buf[32];
                if (f.length >= sizeof buf) return fail(r, f.line, f.column, "this integer does not fit 64 bits");
                memcpy(buf, f.text, f.length);
                buf[f.length] = 0;
                char *end;
                long long v = strtoll(buf, &end, 10);
                if (*end) return fail(r, f.line, f.column, "this integer does not fit 64 bits");
                f.kind = F_INTEGER;
                f.value = v;
            }
        }
    }
    if (r->forms == r->form_cap) {
        size_t cap = r->form_cap ? r->form_cap * 2 : 64;
        Form *grown = realloc(r->form, cap * sizeof *grown);
        if (!grown) return no_memory(r);
        r->form = grown;
        r->form_cap = cap;
    }
    *out = (uint32_t)r->forms;
    r->form[r->forms++] = f;
    return true;
}

static const Form *form_at(const Reader *r, uint32_t i) { return &r->form[i]; }
static const Form *item_of(const Reader *r, const Form *list, uint32_t i) {
    return &r->form[r->item[list->first + i]];
}

static const char *word_text(const Reader *r, const Form *f) {
    return f->kind == F_STRING ? r->bytes + f->value : f->text;
}

static bool is_word(const Reader *r, const Form *f, const char *word) {
    return f->kind == F_SYMBOL && spelled(word, word_text(r, f), f->length);
}

/* ---- Forms to terms ------------------------------------------------------- */

typedef struct {
    const char **name; size_t *length; uint32_t count, cap;   /* variable names */
} Scope;

static uint32_t scope_index(Scope *s, const char *name, size_t length) {
    for (uint32_t i = 0; i < s->count; i++)
        if (s->length[i] == length && memcmp(s->name[i], name, length) == 0) return i;
    if (s->count == s->cap) {
        uint32_t cap = s->cap ? s->cap * 2 : 8;
        const char **n = realloc(s->name, cap * sizeof *n);
        if (!n) return UINT32_MAX;
        s->name = n;
        size_t *l = realloc(s->length, cap * sizeof *l);
        if (!l) return UINT32_MAX;
        s->length = l;
        s->cap = cap;
    }
    s->name[s->count] = name;
    s->length[s->count] = length;
    return s->count++;
}

static void scope_free(Scope *s) { free(s->name); free(s->length); }

/* The term of a form; variables become locals (in a rule) or unknowns (in a row). */
static TcTerm term_of(Reader *r, TcTerms *t, const Form *f, Scope *scope, bool in_rule) {
    switch (f->kind) {
    case F_SYMBOL:   return tc_symbol(t, f->text, f->length);
    case F_INTEGER:  return tc_integer(t, f->value);
    case F_STRING:   return tc_string(t, word_text(r, f), f->length);
    case F_VARIABLE: {
        uint32_t i = scope_index(scope, f->text, f->length);
        if (i == UINT32_MAX) return 0;
        return in_rule ? tc_local(t, i) : tc_unknown(t, i);
    }
    case F_LIST: {
        TcTerm *items = malloc((f->count ? f->count : 1) * sizeof *items);
        if (!items) return 0;
        for (uint32_t i = 0; i < f->count; i++) {
            items[i] = term_of(r, t, item_of(r, f, i), scope, in_rule);
            if (!items[i]) { free(items); return 0; }
        }
        TcTerm out = tc_expression(t, items, f->count);
        free(items);
        return out;
    }
    }
    return 0;
}

/* ---- Rules ----------------------------------------------------------------- */

typedef struct {
    TcTerm *take, *read, *tell, *make;
    size_t takes, take_cap, reads, read_cap, tells, tell_cap, makes, make_cap;
    TcCheck *check; size_t checks, check_cap;
    const Form **check_form;   /* per check: the form it was read from */
} RuleParts;

static void parts_free(RuleParts *p) {
    free(p->take); free(p->read); free(p->tell); free(p->make); free(p->check);
    free(p->check_form);
}

static bool push_check(RuleParts *p, TcCheck c, const Form *form) {
    if (p->checks == p->check_cap) {
        size_t cap = p->check_cap ? p->check_cap * 2 : 4;
        TcCheck *g = realloc(p->check, cap * sizeof *g);
        if (!g) return false;
        p->check = g;
        const Form **f = realloc(p->check_form, cap * sizeof *f);
        if (!f) return false;
        p->check_form = f;
        p->check_cap = cap;
    }
    p->check_form[p->checks] = form;
    p->check[p->checks++] = c;
    return true;
}

/* The two sides of (= A B). */
static bool equation(Reader *r, const Form *f, const Form **a, const Form **b) {
    if (f->kind != F_LIST || f->count != 3 || !is_word(r, item_of(r, f, 0), "="))
        return fail(r, f->line, f->column, "an equation is written (= A B)");
    *a = item_of(r, f, 1);
    *b = item_of(r, f, 2);
    return true;
}

/* The relation and the two sides of a check: (= A B), (/= A B), (< A B) or
 * (<= A B). */
static bool relation(Reader *r, const Form *f, TcCheckKind *kind, const Form **a, const Form **b) {
    const Form *op = f->kind == F_LIST && f->count == 3 ? item_of(r, f, 0) : NULL;
    if (op && is_word(r, op, "=")) *kind = TC_CHECK_EQUAL;
    else if (op && is_word(r, op, "/=")) *kind = TC_CHECK_DIFFERENT;
    else if (op && is_word(r, op, "<")) *kind = TC_CHECK_LESS;
    else if (op && is_word(r, op, "<=")) *kind = TC_CHECK_LESS_EQUAL;
    else return fail(r, f->line, f->column, "a check is written (= A B), (/= A B), (< A B) or (<= A B)");
    *a = item_of(r, f, 1);
    *b = item_of(r, f, 2);
    return true;
}

#define CLAUSE_FORMS \
    "a clause is (take ...), (read ...), (make ...), (tell ...), (check ...), (absent P), (count N P) or (census P)"

static bool read_rule(Reader *r, TcTerms *t, TcProgram *p, const Form *f) {
    if (f->count < 2 || item_of(r, f, 1)->kind != F_SYMBOL)
        return fail(r, f->line, f->column, "a rule is written (rule NAME CLAUSE ...)");
    const Form *name = item_of(r, f, 1);
    for (uint32_t k = 0; k < tc_program_rules(p); k++)   /* a region names rules */
        if (spelled(tc_program_rule_name(p, k), name->text, name->length))
            return fail(r, name->line, name->column, "a rule of this name is already declared");
    Scope scope = {0};
    RuleParts parts = {0};
    bool ok = true;
    for (uint32_t i = 2; ok && i < f->count; i++) {
        const Form *clause = item_of(r, f, i);
        if (clause->kind != F_LIST || clause->count == 0 || item_of(r, clause, 0)->kind != F_SYMBOL) {
            ok = fail(r, clause->line, clause->column, CLAUSE_FORMS);
            break;
        }
        const Form *head = item_of(r, clause, 0);
        TcTerm **list = NULL; size_t *len = NULL, *cap = NULL;
        if (is_word(r, head, "take")) { list = &parts.take; len = &parts.takes; cap = &parts.take_cap; }
        else if (is_word(r, head, "read")) { list = &parts.read; len = &parts.reads; cap = &parts.read_cap; }
        else if (is_word(r, head, "make")) { list = &parts.make; len = &parts.makes; cap = &parts.make_cap; }
        if (list) {
            for (uint32_t k = 1; ok && k < clause->count; k++) {
                TcTerm term = term_of(r, t, item_of(r, clause, k), &scope, true);
                ok = term && tc_push_u32(realloc, list, len, cap, term);
                if (!ok) no_memory(r);
            }
        } else if (is_word(r, head, "tell") || is_word(r, head, "check")) {
            bool tell = is_word(r, head, "tell");
            for (uint32_t k = 1; ok && k < clause->count; k++) {
                const Form *a = NULL, *b = NULL;
                TcCheckKind kind = TC_CHECK_EQUAL;
                if (!(ok = tell ? equation(r, item_of(r, clause, k), &a, &b)
                                : relation(r, item_of(r, clause, k), &kind, &a, &b))) break;
                TcTerm ta = term_of(r, t, a, &scope, true), tb = term_of(r, t, b, &scope, true);
                if (tell)
                    ok = ta && tb && tc_push_u32(realloc, &parts.tell, &parts.tells, &parts.tell_cap, ta) &&
                         tc_push_u32(realloc, &parts.tell, &parts.tells, &parts.tell_cap, tb);
                else
                    ok = ta && tb && push_check(&parts, (TcCheck){kind, ta, tb, 0}, item_of(r, clause, k));
                if (!ok) no_memory(r);
            }
        } else if (is_word(r, head, "absent") || is_word(r, head, "census")) {
            bool absent = is_word(r, head, "absent");
            if (clause->count != 2) {
                ok = fail(r, clause->line, clause->column,
                          absent ? "absence is written (absent P)" : "a census is written (census P)");
                break;
            }
            TcTerm pattern = term_of(r, t, item_of(r, clause, 1), &scope, true);
            ok = pattern && push_check(&parts, (TcCheck){absent ? TC_CHECK_ABSENT : TC_CHECK_CENSUS,
                                                         pattern, 0, 0}, clause);
            if (!ok) no_memory(r);
        } else if (is_word(r, head, "count")) {
            if (clause->count != 3 || item_of(r, clause, 1)->kind != F_INTEGER || item_of(r, clause, 1)->value < 0) {
                ok = fail(r, clause->line, clause->column, "a count is written (count N P), N a natural number");
                break;
            }
            TcTerm pattern = term_of(r, t, item_of(r, clause, 2), &scope, true);
            ok = pattern && push_check(&parts, (TcCheck){TC_CHECK_COUNT, pattern, 0,
                                                         (uint64_t)item_of(r, clause, 1)->value}, clause);
            if (!ok) no_memory(r);
        } else {
            ok = fail(r, clause->line, clause->column, CLAUSE_FORMS);
        }
    }
    if (ok) {
        char *rule_name = malloc(name->length + 1);
        ok = rule_name != NULL;
        if (ok) {
            memcpy(rule_name, name->text, name->length);
            rule_name[name->length] = 0;
            TcRule rule = {
                .name = rule_name,
                .take = parts.take, .takes = (uint32_t)parts.takes,
                .read = parts.read, .reads = (uint32_t)parts.reads,
                .tell = parts.tell, .tells = (uint32_t)(parts.tells / 2),
                .check = parts.check, .checks = (uint32_t)parts.checks,
                .make = parts.make, .makes = (uint32_t)parts.makes,
                .locals = scope.count,
            };
            uint32_t guard = 0, local = 0;
            const char *why = tc_rule_refusal(t, &rule, &guard, &local);
            if (why && guard != UINT32_MAX) {
                const Form *g = parts.check_form[guard];
                char message[sizeof r->error->message];
                if (local != UINT32_MAX)
                    snprintf(message, sizeof message, "%s: %.*s", why,
                             (int)scope.length[local], scope.name[local]);
                else
                    snprintf(message, sizeof message, "%s", why);
                ok = fail(r, g->line, g->column, message);
            } else if (why) {
                ok = no_memory(r);
            } else {
                ok = tc_program_add_rule(p, &rule);
                if (!ok) no_memory(r);
            }
            free(rule_name);
        }
    }
    parts_free(&parts);
    scope_free(&scope);
    return ok;
}

/* ---- Programs -------------------------------------------------------------- */

/* A declaration is made once. */
static bool once(Reader *r, const Form *f, bool *made) {
    if (*made) return fail(r, f->line, f->column, "this declaration is made twice");
    *made = true;
    return true;
}

/* The word of a declaration (WHAT WORD), made once; NULL after an error. */
static const Form *word_arg(Reader *r, const Form *f, const char *what, bool *made) {
    if (!once(r, f, made)) return NULL;
    if (f->count == 2 && item_of(r, f, 1)->kind == F_SYMBOL) return item_of(r, f, 1);
    char message[120];
    snprintf(message, sizeof message, "this declaration is written (%s WORD)", what);
    fail(r, f->line, f->column, message);
    return NULL;
}

TcProgram *tc_read_program(TcTerms *t, const char *text, size_t length,
                           TcDeclarations *d, TcSyntaxError *error) {
    memset(d, 0, sizeof *d);
    memset(error, 0, sizeof *error);
    Reader r = {.text = text, .length = length, .line = 1, .column = 1, .error = error};
    TcProgram *p = tc_program_new(t);
    uint32_t *top = NULL; size_t tops = 0, top_cap = 0;
    bool ok = p != NULL;
    for (;;) {
        skip_space(&r);
        if (!ok || peek(&r) == -1) break;
        uint32_t f;
        ok = read_form(&r, &f) && tc_push_u32(realloc, &top, &tops, &top_cap, f);
    }
    /* rows share their unknowns across the whole program */
    Scope rows = {0};
    /* region names refer to rules, so they are resolved after the rules */
    const Form *region = NULL;
    for (size_t k = 0; ok && k < tops; k++) {
        const Form *f = form_at(&r, top[k]);
        const Form *head = f->kind == F_LIST && f->count > 0 ? item_of(&r, f, 0) : NULL;
        if (head && is_word(&r, head, "rule")) ok = read_rule(&r, t, p, f);
        else if (head && is_word(&r, head, "step")) {
            const Form *w = word_arg(&r, f, "step", &d->has_step);
            ok = w && (tc_step_from_name(w->text, w->length, &d->step) ||
                       fail(&r, w->line, w->column, "a step is one, round, synchronous, bag, every, maximal or unique"));
        } else if (head && is_word(&r, head, "ask")) {
            const Form *w = word_arg(&r, f, "ask", &d->has_ask);
            ok = w && (tc_ask_from_name(w->text, w->length, &d->ask) ||
                       fail(&r, w->line, w->column, "ask is after-tells or before-tells"));
        } else if (head && is_word(&r, head, "store")) {
            const Form *w = word_arg(&r, f, "store", &d->has_store);
            ok = w && (tc_store_from_name(w->text, w->length, &d->store) ||
                       fail(&r, w->line, w->column, "a store is multiset or set"));
        } else if (head && is_word(&r, head, "matches")) {
            const Form *w = word_arg(&r, f, "matches", &d->has_matches);
            ok = w && (tc_matches_from_name(w->text, w->length, &d->matches) ||
                       fail(&r, w->line, w->column, "matches are symmetric or keys"));
        } else if (head && is_word(&r, head, "backend")) {
            const Form *w = word_arg(&r, f, "backend", &d->has_backend);
            ok = w && (tc_backend_from_name(w->text, w->length, &d->backend) ||
                       fail(&r, w->line, w->column, "a backend is c or x86-flat"));
        } else if (head && is_word(&r, head, "observe")) {
            const Form *w = word_arg(&r, f, "observe", &d->has_observe);
            ok = w && (tc_observe_from_name(w->text, w->length, &d->observe) ||
                       fail(&r, w->line, w->column, "observe outcomes or run"));
        } else if (head && is_word(&r, head, "schedule")) {
            const Form *w = word_arg(&r, f, "schedule", &d->has_schedule);
            ok = w && (run_schedule_from_name(w->text, w->length, &d->schedule) ||
                       fail(&r, w->line, w->column, "a schedule is canonical or rotating"));
        } else if (head && is_word(&r, head, "budget")) {
            const Form *w = f->count == 2 ? item_of(&r, f, 1) : NULL;
            ok = once(&r, f, &d->has_budget) &&
                 ((w && w->kind == F_INTEGER && w->value > 0) ||
                  fail(&r, f->line, f->column, "a budget is written (budget N), N positive"));
            if (ok) d->budget = (uint64_t)w->value;
        } else if (head && is_word(&r, head, "tcalc")) {
            const Form *w = f->count == 2 ? item_of(&r, f, 1) : NULL;
            ok = once(&r, f, &d->has_version) &&
                 ((w && (w->kind == F_SYMBOL || w->kind == F_INTEGER)) ||
                  fail(&r, f->line, f->column, "a version is written (tcalc 0.1)"));
            if (ok) {
                size_t n = w->length < sizeof d->version - 1 ? w->length : sizeof d->version - 1;
                memcpy(d->version, w->text, n);
                d->version[n] = 0;
            }
        } else if (head && is_word(&r, head, "region")) {
            ok = once(&r, f, &d->has_region);
            region = f;
        } else if (head && is_word(&r, head, "put")) {
            ok = f->count == 2 || fail(&r, f->line, f->column, "a row is put as (put T)");
            if (ok) {
                TcTerm row = term_of(&r, t, item_of(&r, f, 1), &rows, false);
                ok = row && tc_program_add_row(p, row);
                if (!ok) no_memory(&r);
            }
        } else {
            TcTerm row = term_of(&r, t, f, &rows, false);
            ok = row && tc_program_add_row(p, row);
            if (!ok) no_memory(&r);
        }
    }
    if (ok && region && region->count < 2)
        ok = fail(&r, region->line, region->column, "a region is written (region NAME ...), naming a rule at least");
    if (ok && region) {
        for (uint32_t i = 1; ok && i < region->count; i++) {
            const Form *w = item_of(&r, region, i);
            uint32_t found = UINT32_MAX;
            for (uint32_t k = 0; w->kind == F_SYMBOL && k < tc_program_rules(p); k++)
                if (spelled(tc_program_rule_name(p, k), w->text, w->length)) found = k;
            if (found == UINT32_MAX) { ok = fail(&r, w->line, w->column, "no rule has this name"); break; }
            uint32_t *g = realloc(d->region, (d->regions + 1) * sizeof *g);
            if (!g) { ok = no_memory(&r); break; }
            d->region = g;
            d->region[d->regions++] = found;
        }
    }
    scope_free(&rows);
    free(top); free(r.form); free(r.item); free(r.bytes);
    if (!ok) {
        tc_program_free(p);
        free(d->region);
        d->region = NULL;
        return NULL;
    }
    return p;
}
