#include "regular_pattern_v1.h"
#include "prepared_bytes_v1.h"
#include <stdatomic.h>
#include <stdio.h>

struct CettaRegularPatternV1 { atomic_uint users; RSDFAV1Program program; };

static bool failure(char *error, size_t size) {
    if (error && size) snprintf(error, size, "invalid regular-language pattern image or allocation failure");
    return false;
}

static CettaRegularPatternV1 *adopt(RSDFAV1Program *program, char *error, size_t size) {
    if (program->tag_len != 1u || !rsdfa_v1_program_validate(program, error, size)) return NULL;
    CettaRegularPatternV1 *p = calloc(1u, sizeof(*p));
    if (!p) { failure(error, size); return NULL; }
    atomic_init(&p->users, 1u); p->program = *program;
    rsdfa_v1_program_init(program); return p;
}

bool cetta_regular_pattern_v1_compile(CettaRegularTokenCompilerV1 *compiler,
    Arena *arena, Atom *regex, CettaRegularPatternV1 **out, char *error, size_t size) {
    if (!out) return failure(error, size);
    RSDFAV1Program program = {0};
    if (!arena || !cetta_regular_token_compiler_v1_program(compiler, arena,
        atom_symbol(arena, "regular-pattern"), regex, &program, error, size)) return false;
    CettaRegularPatternV1 *p = adopt(&program, error, size);
    rsdfa_v1_program_free(&program);
    if (!p) return false;
    *out = p; return true;
}

CettaRegularPatternV1 *cetta_regular_pattern_v1_retain(CettaRegularPatternV1 *p) {
    if (!p) return NULL;
    unsigned users = atomic_load(&p->users);
    do { if (!users || users == UINT32_MAX) return NULL; }
    while (!atomic_compare_exchange_weak(&p->users, &users, users + 1u));
    return p;
}
void cetta_regular_pattern_v1_release(CettaRegularPatternV1 *p) {
    if (p && atomic_fetch_sub(&p->users, 1u) == 1u) { rsdfa_v1_program_free(&p->program); free(p); }
}
const RSDFAV1Program *cetta_regular_pattern_v1_program(const CettaRegularPatternV1 *p) {
    return p ? &p->program : NULL;
}

bool cetta_regular_pattern_v1_store(const CettaRegularPatternV1 *p, uint8_t **bytes,
    size_t *len, char *error, size_t size) {
    if (!p || !bytes || !len) return failure(error, size);
    const RSDFAV1Program *d = &p->program;
    CettaPreparedWriterV1 w = {0};
    cpw_bytes(&w, "CRP1", 4u); cpw_text(&w, "regular-single-pattern-v1", strlen("regular-single-pattern-v1"));
    cpw_u64(&w, d->state_len); cpw_u64(&w, d->transition_len);
    cpw_u64(&w, d->accept_tag_len); cpw_u64(&w, d->eof_accept_tag_len);
    cpw_u64(&w, d->start_state); cpw_u64(&w, d->tag_len);
    for (uint32_t i = 0; i < d->state_len; ++i) {
        const RSDFAV1ProgramState *s = &d->states[i];
        cpw_u64(&w, s->transition_begin); cpw_u64(&w, s->transition_len);
        cpw_u64(&w, s->accept_begin); cpw_u64(&w, s->accept_len);
        cpw_u64(&w, s->eof_accept_begin); cpw_u64(&w, s->eof_accept_len);
    }
    for (uint32_t i = 0; i < d->transition_len; ++i) {
        cpw_u64(&w, d->transitions[i].low); cpw_u64(&w, d->transitions[i].high);
        cpw_u64(&w, d->transitions[i].target);
    }
    for (uint32_t i = 0; i < d->accept_tag_len; ++i) cpw_u64(&w, d->accept_tags[i]);
    for (uint32_t i = 0; i < d->eof_accept_tag_len; ++i) cpw_u64(&w, d->eof_accept_tags[i]);
    if (w.failed) { free(w.data); return failure(error, size); }
    *bytes = w.data; *len = w.len; return true;
}

static uint32_t u32(CettaPreparedReaderV1 *r) {
    uint64_t n = cpr_u64(r);
    if (n > UINT32_MAX) r->failed = true;
    return (uint32_t)n;
}

bool cetta_regular_pattern_v1_restore(const uint8_t *bytes, size_t len,
    CettaRegularPatternV1 **out, char *error, size_t size) {
    if (!bytes || !out) return failure(error, size);
    CettaPreparedReaderV1 r = {.data = bytes, .len = len};
    RSDFAV1Program d = {0}; CettaRegularPatternV1 *p = NULL;
    if (!cpr_literal(&r, "CRP1", 4u) || !cpr_abi(&r, "regular-single-pattern-v1")) goto done;
    d.state_len = u32(&r); d.transition_len = u32(&r);
    d.accept_tag_len = u32(&r); d.eof_accept_tag_len = u32(&r);
    d.start_state = u32(&r); d.tag_len = u32(&r);
    if (r.failed || !d.state_len || d.state_len > (r.len - r.at) / 48u ||
        (uint64_t)d.transition_len * 24u + (uint64_t)d.state_len * 48u +
        ((uint64_t)d.accept_tag_len + d.eof_accept_tag_len) * 8u != r.len - r.at) goto done;
    d.states = calloc(d.state_len, sizeof(*d.states));
    d.transitions = calloc(d.transition_len ? d.transition_len : 1u, sizeof(*d.transitions));
    d.accept_tags = calloc(d.accept_tag_len ? d.accept_tag_len : 1u, sizeof(*d.accept_tags));
    d.eof_accept_tags = calloc(d.eof_accept_tag_len ? d.eof_accept_tag_len : 1u, sizeof(*d.eof_accept_tags));
    if (!d.states || !d.transitions || !d.accept_tags || !d.eof_accept_tags) goto done;
    for (uint32_t i = 0; i < d.state_len; ++i) {
        RSDFAV1ProgramState *s = &d.states[i];
        s->transition_begin = u32(&r); s->transition_len = u32(&r);
        s->accept_begin = u32(&r); s->accept_len = u32(&r);
        s->eof_accept_begin = u32(&r); s->eof_accept_len = u32(&r);
    }
    for (uint32_t i = 0; i < d.transition_len; ++i) {
        d.transitions[i].low = u32(&r); d.transitions[i].high = u32(&r); d.transitions[i].target = u32(&r);
    }
    for (uint32_t i = 0; i < d.accept_tag_len; ++i) d.accept_tags[i] = u32(&r);
    for (uint32_t i = 0; i < d.eof_accept_tag_len; ++i) d.eof_accept_tags[i] = u32(&r);
    if (!r.failed && r.at == r.len) p = adopt(&d, error, size);
done:
    rsdfa_v1_program_free(&d);
    if (!p) return failure(error, size);
    *out = p; return true;
}
