#ifndef CETTA_LLM_STREAM_H
#define CETTA_LLM_STREAM_H
#include "atom.h"

/* Buffered transport assembly only. Success is a complete body, not evidence
 * of accounting, authorization or application-level command acceptance. */
Atom *cetta_llm_response_body(Arena *arena, Atom **args, uint32_t nargs);
#endif
