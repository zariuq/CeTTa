/* Behaviour of scanners emitted by emit-regular-span-dfa-v1 from the
 * fixtures of make_fixtures.py: a 45-tag scanner over scalars (prefix kw)
 * and a 4-tag scanner over bytes (prefix by), in one translation unit.
 *
 *   check_emitted KEYWORDS.txt */
#include "kw.h"
#include "by.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        ++checks;                                           \
        if (!(cond)) {                                      \
            ++failures;                                     \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                   \
            fputc('\n', stderr);                            \
        }                                                   \
    } while (0)

static char keywords[40][16];

static unsigned keyword_tag(const char *word) {
    for (unsigned i = 0; i < 40; i++)
        if (strcmp(keywords[i], word) == 0)
            return i;
    fprintf(stderr, "no keyword %s\n", word);
    exit(2);
}

static unsigned kw_run(const unsigned *in, unsigned len) {
    unsigned s = KW_START;
    for (unsigned i = 0; i < len && s != UINT32_MAX; i++)
        s = kw_step(s, in[i]);
    return s;
}

static unsigned kw_run_word(const char *word) {
    unsigned in[32], len = 0;
    for (; word[len]; len++)
        in[len] = (unsigned char)word[len];
    return kw_run(in, len);
}

/* Longest match, the least tag among the longest: ocamllex's and Flex's
 * rule.  At the end of input a state's end-of-input accepts count too. */
typedef struct {
    unsigned tag, end;
} Token;

#define LONGEST_MATCH(NAME, UPPER)                                           \
    static bool NAME##_longest(const unsigned *in, unsigned len, unsigned at, \
                               Token *token) {                               \
        unsigned s = UPPER##_START;                                           \
        token->tag = UINT32_MAX;                                              \
        for (unsigned i = at;; i++) {                                         \
            unsigned w = i == len ? NAME##_eof_winner[s] : NAME##_winner[s];  \
            if (w != UINT32_MAX) {                                            \
                token->tag = w;                                               \
                token->end = i;                                               \
            }                                                                 \
            if (i == len || (s = NAME##_step(s, in[i])) == UINT32_MAX)        \
                break;                                                        \
        }                                                                     \
        return token->tag != UINT32_MAX && token->end > at;                   \
    }
LONGEST_MATCH(kw, KW)
LONGEST_MATCH(by, BY)

/* Decode UTF-8 test text into scalars. */
static unsigned decode(const char *text, unsigned *out) {
    const unsigned char *p = (const unsigned char *)text;
    unsigned n = 0;
    while (*p) {
        unsigned c = *p++;
        if (c >= 0xC0u && c < 0xE0u)
            c = ((c & 0x1Fu) << 6) | (*p++ & 0x3Fu);
        out[n++] = c;
    }
    return n;
}

static void keywords_and_identifiers(void) {
    CHECK(KW_TAG_COUNT == 45u, "kw has %u tags", KW_TAG_COUNT);
    for (unsigned i = 0; i < 40; i++) {
        unsigned s = kw_run_word(keywords[i]);
        CHECK(s != UINT32_MAX, "%s does not run", keywords[i]);
        if (s == UINT32_MAX)
            continue;
        CHECK(kw_winner[s] == i, "%s wins tag %u, not %u", keywords[i], kw_winner[s], i);
        CHECK(kw_accepts(s, kw_tag_ident), "%s is not also an identifier", keywords[i]);
        for (unsigned j = 0; j < 40; j++)
            if (j != i)
                CHECK(!kw_accepts(s, j), "%s accepts keyword %s", keywords[i], keywords[j]);
        CHECK(!kw_accepts(s, kw_tag_layout), "%s accepts layout", keywords[i]);
    }
    const char *plain[] = {"iffy", "inte", "thenx", "f", "z", "interv", "doe", "classes"};
    for (unsigned i = 0; i < sizeof plain / sizeof plain[0]; i++) {
        unsigned s = kw_run_word(plain[i]);
        CHECK(s != UINT32_MAX && kw_winner[s] == kw_tag_ident, "%s is not an identifier", plain[i]);
    }
    CHECK(kw_winner[KW_START] == UINT32_MAX, "the empty input wins a tag");
    CHECK(kw_tag_in == keyword_tag("in") && kw_tag_end == keyword_tag("end"), "tag names out of order");
}

static void tokens(void) {
    struct {
        const char *text;
        unsigned tag;
    } want[] = {
        {"in", 0}, {" ", 0}, {"int", 0}, {" ", 0}, {"inter", 0}, {" ", 0}, {"interval", 0},
        {" ", 0}, {"intervals", 0}, {"\n", 0}, {"forall", 0}, {"\t", 0}, {"fo", 0}, {" ", 0},
        {"..", 0}, {" ", 0}, {"\xce\xb1\xce\xb2\xce\xb3", 0}, {" ", 0}, {"x", 0}, {".", 0},
    };
    unsigned n = sizeof want / sizeof want[0];
    for (unsigned i = 0; i < n; i++) {
        const char *t = want[i].text;
        if (t[0] == ' ' || t[0] == '\n' || t[0] == '\t')
            want[i].tag = kw_tag_layout;
        else if (strcmp(t, "..") == 0)
            want[i].tag = kw_tag_dotdot;
        else if (strcmp(t, ".") == 0)
            want[i].tag = kw_tag_dot_at_end;
        else if ((unsigned char)t[0] >= 0x80u)
            want[i].tag = kw_tag_greek;
        else if (strcmp(t, "intervals") == 0 || strcmp(t, "fo") == 0 || strcmp(t, "x") == 0)
            want[i].tag = kw_tag_ident;
        else
            want[i].tag = keyword_tag(t);
    }
    char text[256] = "";
    for (unsigned i = 0; i < n; i++)
        strcat(text, want[i].text);
    unsigned in[256], len = decode(text, in), at = 0;
    for (unsigned i = 0; i < n; i++) {
        Token token;
        unsigned scalars[64], width = decode(want[i].text, scalars);
        bool ok = kw_longest(in, len, at, &token);
        CHECK(ok && token.tag == want[i].tag && token.end == at + width,
              "token %u (%s): tag %u end %u, wanted tag %u end %u", i, want[i].text,
              ok ? token.tag : UINT32_MAX, ok ? token.end : 0u, want[i].tag, at + width);
        at += width;
    }
    CHECK(at == len, "tokens stop at %u of %u", at, len);
    /* "." is a token only at the end of input; ".." anywhere. */
    unsigned mid[] = {'x', '.', ' ', 'y'};
    Token token;
    CHECK(kw_longest(mid, 4, 0, &token) && token.tag == kw_tag_ident && token.end == 1, "x before .");
    CHECK(!kw_longest(mid, 4, 1, &token), "a lone . before layout is a token");
    unsigned dots[] = {'.', '.', '.'};
    CHECK(kw_longest(dots, 3, 0, &token) && token.tag == kw_tag_dotdot && token.end == 2, "... starts with ..");
    CHECK(kw_longest(dots, 3, 2, &token) && token.tag == kw_tag_dot_at_end && token.end == 3, "... ends with .");
    unsigned s = kw_step(KW_START, '.');
    CHECK(s != UINT32_MAX && !kw_accepts(s, kw_tag_dot_at_end) && kw_eof_accepts(s, kw_tag_dot_at_end),
          ". accepts only at the end");
}

static void above_ascii(void) {
    CHECK(kw_step(KW_START, 0x3B1u) != UINT32_MAX, "alpha has no edge");
    CHECK(kw_step(KW_START, 0x3C9u) != UINT32_MAX, "omega has no edge");
    CHECK(kw_step(KW_START, 0x3B0u) == UINT32_MAX, "U+03B0 has an edge");
    CHECK(kw_step(KW_START, 0x3CAu) == UINT32_MAX, "U+03CA has an edge");
    CHECK(kw_step(KW_START, 0x10FFFFu) == UINT32_MAX, "U+10FFFF has an edge");
    /* The direct table and the interval search agree with a plain scan of
     * each state's edges, at every edge boundary and on all of 0..1023. */
    unsigned states = sizeof kw_offsets / sizeof kw_offsets[0], disagree = 0;
    for (unsigned st = 0; st < states; st++) {
        unsigned begin = kw_offsets[st][0], count = kw_offsets[st][1];
        unsigned probes[4096], np = 0;
        for (unsigned u = 0; u < 1024u; u++)
            probes[np++] = u;
        for (unsigned e = begin; e < begin + count && np + 4 < 4096; e++) {
            probes[np++] = kw_edges[e].low ? kw_edges[e].low - 1u : 0u;
            probes[np++] = kw_edges[e].low;
            probes[np++] = kw_edges[e].high;
            probes[np++] = kw_edges[e].high + 1u;
        }
        for (unsigned k = 0; k < np; k++) {
            unsigned u = probes[k], plain = UINT32_MAX;
            for (unsigned e = begin; e < begin + count; e++)
                if (kw_edges[e].low <= u && u <= kw_edges[e].high)
                    plain = kw_edges[e].target;
            disagree += plain != kw_step(st, u);
        }
    }
    CHECK(disagree == 0, "%u step results differ from the edge lists", disagree);
}

static void byte_scanner(void) {
    CHECK(BY_TAG_COUNT == 4u, "by has %u tags", BY_TAG_COUNT);
    for (unsigned b = 0; b < 256; b++) {
        bool edge = b == 0 || b >= 0x80u || (b >= 'a' && b <= 'z');
        CHECK((by_step(BY_START, b) != UINT32_MAX) == edge, "byte %u", b);
    }
    unsigned states = sizeof by_offsets / sizeof by_offsets[0];
    for (unsigned st = 0; st < states; st++)
        CHECK(by_step(st, 256u) == UINT32_MAX, "unit 256 steps from state %u", st);
    unsigned in[] = {'a', 'b', 0xFF, 0xFE, 0x00, 'a', 'b', 'c'};
    struct { unsigned tag, end; } want[] = {
        {by_tag_ab, 2}, {by_tag_high, 4}, {by_tag_nul, 5}, {by_tag_lower, 8},
    };
    unsigned at = 0;
    for (unsigned i = 0; i < 4; i++) {
        Token token;
        bool ok = by_longest(in, 8, at, &token);
        CHECK(ok && token.tag == want[i].tag && token.end == want[i].end,
              "byte token %u: tag %u end %u", i, ok ? token.tag : UINT32_MAX, ok ? token.end : 0u);
        at = want[i].end;
    }
}

int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    FILE *f = fopen(argv[1], "r");
    if (!f)
        return 2;
    for (unsigned i = 0; i < 40; i++)
        if (fscanf(f, "%15s", keywords[i]) != 1)
            return 2;
    fclose(f);
    keywords_and_identifiers();
    tokens();
    above_ascii();
    byte_scanner();
    printf("regular-span DFA emitter: %d of %d checks pass\n", checks - failures, checks);
    return failures ? 1 : 0;
}
