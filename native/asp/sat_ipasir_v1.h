#ifndef CETTA_SAT_IPASIR_V1_H
#define CETTA_SAT_IPASIR_V1_H

#include <stdbool.h>
#include <stdint.h>

/* An IPASIR SAT solver, loaded at run time as a reference for the SAT
 * personality: the library is CETTA_IPASIR_LIBRARY when set, else
 * libcadical.so on the loader's path; where none loads nothing here
 * applies.  Clauses are DIMACS literals over variables 1..nvars. */

bool sat_ipasir_available(void);
/* The loaded solver's signature, or NULL. */
const char *sat_ipasir_signature(void);

typedef enum {
    SAT_IPASIR_SAT = 10,
    SAT_IPASIR_UNSAT = 20,
    SAT_IPASIR_FAILED = 0,   /* not loaded, out of memory, or no verdict */
} SatIpasirResult;

/* Solve the clauses (clause i is lits[start[i]] .. lits[start[i + 1] - 1]);
 * on SAT_IPASIR_SAT, values[v] (for 1 <= v <= nvars) is 1 when v is true. */
SatIpasirResult sat_ipasir_solve(uint32_t nvars, const int32_t *lits,
                                 const uint32_t *start, uint32_t nclauses,
                                 uint8_t *values);

#endif
