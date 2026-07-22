#ifndef CETTA_NAME_KEY_H
#define CETTA_NAME_KEY_H

#include "atom.h"

typedef struct NameKeyTable NameKeyTable;

/* Structural names are closed, acyclic atoms whose hashes are independent of
   runtime context.  This predicate performs admission without interning. */
bool name_key_is_admissible(Atom *key);

/* The table owns collision-checked structural copies of every key. */
NameKeyTable *name_key_table_new(size_t bucket_count);

/* Diagnostic constructor used to exercise real digest collisions.  Identity
   still requires structural equality, even when the mask is zero. */
NameKeyTable *name_key_table_new_with_hash_mask(size_t bucket_count,
                                                uint32_t hash_mask);
void name_key_table_delete(NameKeyTable *table);

/* Open variables, cyclic graphs, and contextual grounded values fail closed. */
NameId name_key_intern(NameKeyTable *table, Atom *key);
/* Non-mutating lookup.  Unknown or inadmissible keys return NAME_ID_NONE. */
NameId name_key_find(const NameKeyTable *table, Atom *key);
const Atom *name_key_lookup(const NameKeyTable *table, NameId id);
uint32_t name_key_count(const NameKeyTable *table);

#endif /* CETTA_NAME_KEY_H */
