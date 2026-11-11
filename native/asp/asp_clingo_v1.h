#ifndef CETTA_ASP_CLINGO_V1_H
#define CETTA_ASP_CLINGO_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* clingo, loaded at run time as a reference: its grounder turns a program
 * text into ground statements, and its solver answers a ground program.
 * The library is CETTA_CLINGO_LIBRARY when set, else libclingo.so.4 on the
 * loader's path; where it cannot be loaded nothing here applies. */

typedef uint64_t AspClingoSymbol;

typedef enum {
    ASP_CLINGO_SYMBOL_NUMBER,
    ASP_CLINGO_SYMBOL_STRING,
    ASP_CLINGO_SYMBOL_FUNCTION,
    ASP_CLINGO_SYMBOL_OTHER,
} AspClingoSymbolKind;

/* What a ground program reports, statement by statement: a rule (a choice
 * when `choice`), a weight rule over weighted literals, a minimize statement,
 * and the symbol an output atom names (atom 0: a fact that always holds).
 * `unsupported` names a statement kind with no reading here. */
typedef struct {
    bool (*rule)(void *context, bool choice, const uint32_t *head,
                 size_t head_len, const int32_t *body, size_t body_len);
    bool (*weight_rule)(void *context, bool choice, const uint32_t *head,
                        size_t head_len, int32_t lower,
                        const int32_t *literals, const int32_t *weights,
                        size_t body_len);
    bool (*output)(void *context, AspClingoSymbol symbol, uint32_t atom);
    bool (*unsupported)(void *context, const char *what);
    /* a minimize statement: at `priority`, the weights of the true literals
     * (weights may be negative); NULL reports it unsupported */
    bool (*minimize)(void *context, int32_t priority, const int32_t *literals,
                     const int32_t *weights, size_t len);
} AspClingoGroundSink;

bool asp_clingo_available(void);

/* Ground `text` (the base part); false with *error set where clingo
 * refuses it or a sink callback stops. */
bool asp_clingo_ground(const char *text, const AspClingoGroundSink *sink,
                       void *context, const char **error);

AspClingoSymbolKind asp_clingo_symbol_kind(AspClingoSymbol symbol);
bool asp_clingo_symbol_number(AspClingoSymbol symbol, int *number);
bool asp_clingo_symbol_string(AspClingoSymbol symbol, const char **text);
/* A function symbol's name (empty for a tuple), its arguments, and
 * whether it is classically negated. */
bool asp_clingo_symbol_function(AspClingoSymbol symbol, const char **name,
                                const AspClingoSymbol **arguments,
                                size_t *count, bool *negative);

/* A ground normal program over atoms 1..atom_count, solved by clingo:
 * rule r is head[r] :- pos..., not neg..., a head of 0 a constraint. */
typedef struct {
    uint32_t head;
    const uint32_t *pos;
    size_t pos_len;
    const uint32_t *neg;
    size_t neg_len;
    bool choice;
} AspClingoRule;

/* What a model callback answers: on to the next model, stop here (the
 * search ends without error), or fail. */
typedef enum {
    ASP_CLINGO_MORE,
    ASP_CLINGO_STOP,
    ASP_CLINGO_FAIL,
} AspClingoNext;

/* The stable models, as calls of `model` with the true atoms in increasing
 * order, until there are no more or `model` stops; false with *error set
 * where clingo or `model` fails. */
bool asp_clingo_models(uint32_t atom_count, const AspClingoRule *rules,
                       size_t rule_count,
                       AspClingoNext (*model)(void *context,
                                              const uint32_t *atoms,
                                              size_t len),
                       void *context, const char **error);

/* The stable models of the program TEXT (its base part), as clingo grounds
 * and solves it: calls of `model` with the shown symbols true in it, until
 * there are no more or `model` stops. */
bool asp_clingo_text_models(const char *text,
                            AspClingoNext (*model)(void *context,
                                                   const AspClingoSymbol *symbols,
                                                   size_t len),
                            void *context, const char **error);

#endif
