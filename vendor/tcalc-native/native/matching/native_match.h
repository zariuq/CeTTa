#ifndef TCALC_NATIVE_MATCH_H
#define TCALC_NATIVE_MATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint64_t kind, a, b, c; } NmNode;
typedef enum {
    NM_NODE_CONSTANT=0, NM_NODE_VARIABLE=1, NM_NODE_TUPLE=2
} NmNodeKind;
typedef struct { uint64_t variable, scope; } NmVariableKey;
typedef struct { uint64_t left, right; } NmNodePair;

typedef enum { NM_REFERENCE, NM_NATIVE, NM_COMPARE } NmExecution;
typedef enum {
    NM_NO_MATCH = 0, NM_UNIFIED = 1, NM_UNSUPPORTED = 2,
    NM_INVALID = 3, NM_EXHAUSTED = 4
} NmOutcome;

typedef struct {
    const NmNode *nodes; size_t nodes_count;
    const uint64_t *children; size_t children_count;
    const uint64_t *initial; size_t variables_count;
    const NmVariableKey *keys; size_t keys_count;
    uint64_t left, right;
} NmUnifyInput;

/* Caller-owned, mutually disjoint storage, lent exclusively for the call.
 * No pointer is retained. Input owners and their lexical interpretations
 * must remain stable for the call and for projection of returned node ids. */
typedef struct {
    uint64_t *bindings; size_t bindings_words;
    NmNodePair *pairs; size_t pairs_count;
    uint64_t *scan; size_t scan_words;
    uint64_t *state; size_t state_words;
    uint64_t *marks; size_t marks_words;
} NmUnifyWork;

typedef struct {
    NmOutcome outcome;
    uint64_t payload, fuel_used;
} NmUnifyResult;

typedef struct NmUnifyProgram NmUnifyProgram;
typedef enum {
    NM_CALL_OK, NM_CALL_BAD_ARGUMENT, NM_CALL_RESOURCE, NM_CALL_DIFFERENCE
} NmCallStatus;
typedef enum {
    NM_TRUTH_UNAVAILABLE=0, NM_TRUTH_ENTAILED, NM_TRUTH_REFUTED, NM_TRUTH_OPEN
} NmConstraintTruth;

/* Load the program text authored by unify.metta. Native mode checks its
 * emitted bytes against the linked assembly image. Reference-only loading
 * needs no native emission. Neither mode maps executable memory. */
NmUnifyProgram *nm_unify_program_open(const char *path, bool native,
                                     char *error, size_t error_size);
/* The packaged component needs no file, code generation or executable mapping. */
NmUnifyProgram *nm_unify_program_default(bool native, char *error, size_t error_size);
void nm_unify_program_close(NmUnifyProgram *program);
size_t nm_unify_state_words(const NmUnifyProgram *program);
const uint8_t *nm_unify_native_bytes(const NmUnifyProgram *program, size_t *size);

/* True means a declared machine outcome was produced, including NO_MATCH,
 * UNSUPPORTED and EXHAUSTED. False is a host/setup or differential error;
 * no result may be published. The initial environment is immutable.
 * Compare mode executes isolated copies, checks every region and guard,
 * then copies agreed private work back to the caller. */
bool nm_unify_run(const NmUnifyProgram *program, NmExecution execution,
                  const NmUnifyInput *input, const NmUnifyWork *work,
                  uint64_t fuel_budget, uint64_t fuel_used,
                  NmUnifyResult *result, char *error, size_t error_size);
/* The same operation with a structured host failure cause. A declared machine
 * outcome (including EXHAUSTED or UNSUPPORTED) returns NM_CALL_OK. Every other
 * status prohibits result publication. */
NmCallStatus nm_unify_call(const NmUnifyProgram *program, NmExecution execution,
                           const NmUnifyInput *input, const NmUnifyWork *work,
                           uint64_t fuel_budget, uint64_t fuel_used,
                           NmUnifyResult *result, char *error, size_t error_size);
/* For this finite first-order component: zero new bindings means the tested
 * constraint already holds under the initial substitution. Nonzero new
 * bindings establish a possible extension, not an entailed guard. REFUSED
 * means no finite unifier. Operational failures supply no logical verdict;
 * their original call status/outcome/reason remain the diagnostic carrier. */
NmConstraintTruth nm_constraint_truth(NmCallStatus status, const NmUnifyResult *result);

#ifdef __cplusplus
}
#endif
#endif
