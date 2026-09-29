#include "atom.h"
#include "parser.h"
#include "str_natives.h"
#include "string_ops.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Strings are bytes plus a length.  These cases check the atom contract for
 * strings that hold NUL and bytes outside UTF-8: construction, equality,
 * hashing, copying, the term store, printing and reading back; and the byte
 * and codepoint operations, whose codepoint side refuses malformed UTF-8 at
 * the offset of the first invalid sequence.
 */

static int passed;
static int failed;

static void expect(bool ok, const char *what) {
    if (ok) {
        passed++;
    } else {
        failed++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static Atom *bytes(Arena *a, const char *text, size_t len) {
    return atom_string_n(a, text, len);
}

static void construction(Arena *a) {
    Atom *nul = bytes(a, "a\0b", 3u);
    expect(atom_string_len(nul) == 3u, "a string holding NUL keeps its length");
    expect(nul->ground.sval[3] == '\0', "a string keeps a trailing NUL for C");
    expect(memcmp(nul->ground.sval, "a\0b", 3u) == 0,
           "a string keeps every byte, NUL included");
    Atom *empty = bytes(a, "", 0u);
    expect(atom_string_len(empty) == 0u, "the empty string has length zero");
    Atom *c = atom_string(a, "abc");
    expect(atom_string_len(c) == 3u, "a C string's length is its strlen");
}

static void equality_and_hashing(Arena *a) {
    Atom *left = bytes(a, "a\0b", 3u);
    Atom *same = bytes(a, "a\0b", 3u);
    Atom *other = bytes(a, "a\0c", 3u);
    Atom *prefix = bytes(a, "a", 1u);
    expect(atom_eq(left, same), "equal bytes are equal strings");
    expect(!atom_eq(left, other), "a difference after NUL is a difference");
    expect(!atom_eq(prefix, left), "a proper prefix is not equal");
    expect(atom_hash(left) == atom_hash(same), "equal strings hash equally");
    expect(atom_hash(left) != atom_hash(other),
           "these two strings, differing after NUL, hash apart");
    expect(atom_string_compare(prefix, left) < 0, "a proper prefix orders first");
    expect(atom_string_compare(left, other) < 0, "bytes order after NUL");
    expect(atom_eq(atom_string(a, "plain"), bytes(a, "plain", 5u)),
           "a C string and its bytes make the same string");
}

static void copying(Arena *a) {
    Arena other;
    arena_init(&other);
    Atom *source = bytes(a, "x\0\xffy", 4u);
    Atom *copy = atom_deep_copy(&other, source);
    expect(copy && atom_string_len(copy) == 4u &&
               memcmp(copy->ground.sval, "x\0\xffy", 4u) == 0,
           "a deep copy keeps every byte and the length");
    expect(copy && atom_eq(copy, source), "a deep copy is equal");
    arena_free(&other);
}

static void term_store(Arena *a) {
    Arena persistent;
    arena_init(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    AtomId id = tu_intern_string_n(&universe, "p\0q", 3u);
    expect(id != CETTA_ATOM_ID_NONE, "the term store interns a string holding NUL");
    expect(tu_string_len(&universe, id) == 3u, "the term store keeps the length");
    AtomId again = tu_intern_string_n(&universe, "p\0q", 3u);
    AtomId other = tu_intern_string_n(&universe, "p\0r", 3u);
    expect(again == id, "equal strings share one entry");
    expect(other != id, "strings differing after NUL are two entries");
    Atom *back = term_universe_copy_atom(&universe, a, id);
    expect(back && atom_eq(back, bytes(a, "p\0q", 3u)),
           "a stored string reads back with every byte");
    term_universe_free(&universe);
    arena_free(&persistent);
}

static void printing_and_reading(Arena *a) {
    Atom *value = bytes(a, "a\0b\x01\xc3\xa9\xff\"\\\n", 10u);
    char *printed = atom_to_parseable_string(a, value);
    const char *expected = "\"a\\x00b\\x01\xc3\xa9\\xff\\\"\\\\\\n\"";
    expect(printed && strcmp(printed, expected) == 0,
           "control bytes and bytes outside UTF-8 print as \\x escapes");
    if (printed && strcmp(printed, expected) != 0)
        fprintf(stderr, "  printed %s\n", printed);
    size_t pos = 0u;
    Atom *read = printed ? parse_sexpr(a, printed, &pos) : NULL;
    expect(read && atom_eq(read, value), "the printed string reads back exactly");
}

static void codepoints(void) {
    size_t count = 0u, bad = 0u;
    expect(cetta_utf8_count("\xc3\xa9", 2u, &count, &bad) && count == 1u,
           "e with an acute accent is one codepoint of two bytes");
    expect(cetta_utf8_count("a\0b", 3u, &count, &bad) && count == 3u,
           "NUL is a codepoint");
    expect(!cetta_utf8_count("ab\xff", 3u, &count, &bad) && bad == 2u,
           "malformed UTF-8 is refused at its offset");
    expect(!cetta_utf8_count("a\xc3", 2u, &count, &bad) && bad == 1u,
           "a truncated sequence is refused at its start");
    expect(!cetta_utf8_count("\xed\xa0\x80", 3u, &count, &bad) && bad == 0u,
           "a surrogate is not a scalar value");
    expect(!cetta_utf8_count("\xc0\xaf", 2u, &count, &bad) && bad == 0u,
           "an overlong encoding is refused");
}

static void escape_round_trip(CettaQuotingScheme scheme, const char *text,
                              size_t len, const char *what) {
    CettaByteBuffer escaped, back;
    cetta_byte_buffer_init(&escaped);
    cetta_byte_buffer_init(&back);
    bool ok = cetta_string_escape(scheme, text, len, &escaped, NULL) &&
              cetta_string_unescape(scheme, escaped.bytes ? escaped.bytes : "",
                                    escaped.len, &back, NULL) &&
              back.len == len && memcmp(back.bytes, text, len) == 0;
    expect(ok, what);
    cetta_byte_buffer_free(&escaped);
    cetta_byte_buffer_free(&back);
}

static void escaping(void) {
    escape_round_trip(CETTA_QUOTING_METTA, "a\0b\"\\\n\xff", 7u,
                      "the MeTTa scheme round-trips any bytes");
    escape_round_trip(CETTA_QUOTING_JSON, "a\"b\n\x01\xc3\xa9", 7u,
                      "the JSON scheme round-trips UTF-8 text");
    escape_round_trip(CETTA_QUOTING_TPTP_SINGLE, "it's a\\b", 8u,
                      "the TPTP quoted-atom scheme round-trips");
    escape_round_trip(CETTA_QUOTING_TPTP_DOUBLE, "say \"hi\"", 8u,
                      "the TPTP distinct-object scheme round-trips");
    CettaByteBuffer out;
    size_t bad = 99u;
    cetta_byte_buffer_init(&out);
    expect(!cetta_string_escape(CETTA_QUOTING_TPTP_SINGLE, "a\xc3\xa9", 3u, &out, &bad) &&
               bad == 1u,
           "TPTP quoted text refuses a byte outside printable ASCII");
    cetta_byte_buffer_free(&out);
    cetta_byte_buffer_init(&out);
    expect(!cetta_string_unescape(CETTA_QUOTING_JSON, "\\ud800", 6u, &out, &bad) &&
               bad == 0u,
           "JSON refuses a lone surrogate escape");
    cetta_byte_buffer_free(&out);
    cetta_byte_buffer_init(&out);
    expect(cetta_string_unescape(CETTA_QUOTING_JSON, "\\ud83d\\ude00", 12u, &out, &bad) &&
               out.len == 4u && memcmp(out.bytes, "\xf0\x9f\x98\x80", 4u) == 0,
           "JSON joins a surrogate pair into one scalar");
    cetta_byte_buffer_free(&out);
}

static bool spelled(const char *text, size_t len, const CettaCharClass *initial,
                    const CettaCharClass *rest) {
    bool holds = false;
    size_t bad = 0u;
    return cetta_utf8_spelled(text, len, initial, rest, &holds, &bad) && holds;
}

static void spelling(void) {
    const CettaCharClass lower = CETTA_CHAR_CLASS_LOWER;
    const CettaCharClass upper = CETTA_CHAR_CLASS_UPPER;
    const CettaCharClass word = CETTA_CHAR_CLASS_WORD;
    const CettaCharClass alnum = CETTA_CHAR_CLASS_ALNUM;
    expect(spelled("abc_1", 5u, &lower, &word), "abc_1 is a lower word");
    expect(!spelled("Abc", 3u, &lower, &word), "Abc is not a lower word");
    expect(!spelled("", 0u, &lower, &word), "the empty string is not a lower word");
    expect(spelled("X1", 2u, &upper, &word), "X1 is an upper word");
    expect(spelled("", 0u, NULL, &alnum), "every codepoint of the empty string is alnum");
    expect(!spelled("a_b", 3u, NULL, &alnum), "_ is not alnum");
    expect(!spelled("\xc3\xa9", 2u, NULL, &alnum), "a codepoint beyond ASCII is in no class");
    bool holds = true;
    size_t bad = 99u;
    expect(!cetta_utf8_spelled("ab\xff", 3u, &lower, NULL, &holds, &bad) && bad == 2u,
           "a class test refuses malformed UTF-8 at its offset, whatever came first");
    CettaCharClass cls;
    expect(cetta_char_class_from_name("xdigit", &cls) &&
               cetta_char_class_holds(cls, 'F') && !cetta_char_class_holds(cls, 'g'),
           "xdigit holds F and not g");
    expect(!cetta_char_class_from_name("letter", &cls), "an unknown class name is refused");
}

static Atom *call_str(Arena *a, const char *name, Atom **args,
                      CettaStrRefusal *refusal) {
    const CettaStrNative *native = cetta_str_native(name);
    return native ? native->fn(a, args, refusal) : NULL;
}

static void natives(Arena *a) {
    CettaStrRefusal refusal = {0};
    Atom *acute[1] = {atom_string(a, "\xc3\xa9")};
    Atom *length = call_str(a, "byte-length", acute, &refusal);
    Atom *count = call_str(a, "codepoint-count", acute, &refusal);
    expect(length && length->ground.ival == 2, "str:byte-length of e-acute is 2");
    expect(count && count->ground.ival == 1, "str:codepoint-count of e-acute is 1");
    Atom *malformed[1] = {bytes(a, "ok\xc3", 3u)};
    refusal = (CettaStrRefusal){0};
    expect(!call_str(a, "codepoint-count", malformed, &refusal) && refusal.reason &&
               strcmp(refusal.reason, "StrMalformedUtf8V1") == 0 && refusal.offset == 2u,
           "str:codepoint-count refuses a truncated sequence at its offset");
    Atom *nul[1] = {bytes(a, "a\0b", 3u)};
    refusal = (CettaStrRefusal){0};
    expect(!call_str(a, "string->symbol", nul, &refusal) && refusal.reason &&
               strcmp(refusal.reason, "StrNulInSymbolV1") == 0 && refusal.offset == 1u,
           "a symbol cannot hold NUL");
    Atom *word[1] = {atom_string(a, "cat")};
    Atom *symbol = call_str(a, "string->symbol", word, &refusal);
    Atom *back = symbol ? call_str(a, "symbol->string", &symbol, &refusal) : NULL;
    expect(back && atom_eq(back, word[0]), "string->symbol->string is the identity");
    Atom *huge[1] = {atom_string(a, "1e999")};
    refusal = (CettaStrRefusal){0};
    expect(!call_str(a, "string->number", huge, &refusal) && refusal.reason &&
               strcmp(refusal.reason, "StrNumberOutOfRangeV1") == 0,
           "a float beyond the finite doubles is refused");
    Atom *partial[1] = {atom_string(a, "12x")};
    refusal = (CettaStrRefusal){0};
    expect(!call_str(a, "string->number", partial, &refusal) && refusal.reason &&
               refusal.offset == 2u,
           "a number is read whole or refused where it stops");
    Atom *members[3] = {atom_string(a, "b"), atom_string(a, "a"), atom_string(a, "b")};
    Atom *list[1] = {atom_list(a, members, 3u)};
    Atom *set = call_str(a, "set", list, &refusal);
    expect(set && set->kind == ATOM_EXPR && set->expr.len == 2u &&
               atom_list_len(set->expr.elems[1]) == 2u,
           "a set keeps each member once");
    Atom *probe[2] = {set, atom_string(a, "a")};
    Atom *miss[2] = {set, atom_string(a, "c")};
    Atom *hit = set ? call_str(a, "set-member?", probe, &refusal) : NULL;
    Atom *none = set ? call_str(a, "set-member?", miss, &refusal) : NULL;
    expect(hit && hit->ground.gkind == GV_BOOL && hit->ground.bval, "a member is found");
    expect(none && none->ground.gkind == GV_BOOL && !none->ground.bval, "a stranger is not");
    expect(!cetta_str_native("no-such-operation"), "an unknown operation has no native");
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    construction(&arena);
    equality_and_hashing(&arena);
    copying(&arena);
    term_store(&arena);
    printing_and_reading(&arena);
    codepoints();
    escaping();
    spelling();
    natives(&arena);

    arena_free(&arena);
    printf("string atoms: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
