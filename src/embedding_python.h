#ifndef CETTA_EMBEDDING_PYTHON_H
#define CETTA_EMBEDDING_PYTHON_H
#include "cetta_embed.h"
/* Private adapter seam; the public C ABI remains independent of Python. */
cetta_status_t cetta_embedding_python_object(cetta_engine_t *, void *, cetta_atom_t **);
void *cetta_embedding_python_borrow(const cetta_atom_t *);
void *cetta_embedding_python_exception(const cetta_atom_t *, int raised);
void *cetta_embedding_python_attach(cetta_engine_t *);
void *cetta_embedding_python_atom_owner(const cetta_atom_t *);
void *cetta_embedding_python_space_owner(const cetta_space_t *);
void *cetta_embedding_python_query_owner(const cetta_query_t *);
cetta_status_t cetta_embedding_query_values(cetta_engine_t *, const cetta_atom_t *,
    const cetta_eval_options_t *, cetta_query_t **);
#endif
