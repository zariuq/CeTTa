/* tcalc_syntax.h - the reader of .tcalc programs.
 *
 * A program is a sequence of S-expressions in MeTTa's lexical conventions
 * (';' starts a comment).  The forms whose head is one of the reserved words
 * below are declarations; every other top-level form is a row of the initial
 * state, exactly as in a MeTTa file.
 *
 *   (tcalc 0.1)            the version of the calculus the program is written for
 *   (step one)             one round synchronous bag every maximal unique
 *   (ask after-tells)      or before-tells
 *   (store multiset)       or set
 *   (matches symmetric)    or keys
 *   (backend c)            or x86-flat
 *   (observe outcomes)     or run
 *   (schedule canonical)   run: how its steps are chosen
 *   (budget N)             units of work for the exploration
 *   (region NAME ...)      every and unique: the rules they range over
 *   (rule NAME CLAUSE ...) a rule; its clauses, each any number of times:
 *        (take P ...) (read P ...) (make P ...)
 *        (tell (= A B) ...)  (absent P)  (count N P)  (census P)
 *        (check C ...), each C an equation (= A B) or a guard (/= A B), (< A B),
 *        (<= A B); an operand of < and <= may be a sum (+ A B).  A guard reads
 *        only variables of the rule's take and read patterns (tcalc_core.h).
 *   (put T)                a row whose head is a reserved word
 *
 * Each declaration is made at most once, and rule names are distinct.  In a
 * rule, $name is one of the rule's own unknowns.  In a row, $name is an unknown
 * of the initial state, shared by the rows that mention it. */
#ifndef CETTA_TCALC_SYNTAX_H
#define CETTA_TCALC_SYNTAX_H

#include "tcalc_core.h"

typedef struct {
    bool has_version, has_step, has_ask, has_store, has_matches, has_backend, has_observe,
        has_schedule, has_budget, has_region;
    char version[32];
    TcStep step;
    TcAsk ask;
    TcStore store;
    TcMatches matches;
    TcBackend backend;
    TcObserve observe;
    RunSchedule schedule;
    uint64_t budget;
    uint32_t *region;   /* rule indices; owned, free() it */
    uint32_t regions;
} TcDeclarations;

typedef struct {
    size_t line, column;   /* 1-based position of the offending form */
    char message[200];
} TcSyntaxError;

/* Reads a program.  NULL on a syntax error (described in *error) or when memory
 * runs out (error->line is then 0). */
TcProgram *tc_read_program(TcTerms *terms, const char *text, size_t length,
                           TcDeclarations *declarations, TcSyntaxError *error);

/* The spellings of the axes, shared by the reader, the CLI and the output. */
const char *tc_step_name(TcStep step);
const char *tc_ask_name(TcAsk ask);
const char *tc_store_name(TcStore store);
const char *tc_matches_name(TcMatches matches);
const char *tc_backend_name(TcBackend backend);
bool tc_backend_from_name(const char *name, size_t length, TcBackend *backend);
const char *tc_observe_name(TcObserve observe);
bool tc_observe_from_name(const char *name, size_t length, TcObserve *observe);
bool tc_step_from_name(const char *name, size_t length, TcStep *step);
bool tc_ask_from_name(const char *name, size_t length, TcAsk *ask);
bool tc_store_from_name(const char *name, size_t length, TcStore *store);
bool tc_matches_from_name(const char *name, size_t length, TcMatches *matches);

#endif
