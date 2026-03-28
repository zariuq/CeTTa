#ifndef CETTA_MORK_SPACE_BRIDGE_RUNTIME_H
#define CETTA_MORK_SPACE_BRIDGE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaMorkSpaceHandle CettaMorkSpaceHandle;

bool cetta_mork_bridge_is_available(void);
const char *cetta_mork_bridge_last_error(void);

CettaMorkSpaceHandle *cetta_mork_bridge_space_new(void);
void cetta_mork_bridge_space_free(CettaMorkSpaceHandle *space);

bool cetta_mork_bridge_space_clear(CettaMorkSpaceHandle *space);
bool cetta_mork_bridge_space_add_indexed_sexpr(CettaMorkSpaceHandle *space,
                                               uint32_t atom_idx,
                                               const uint8_t *text,
                                               size_t len);
bool cetta_mork_bridge_space_query_indices(CettaMorkSpaceHandle *space,
                                           const uint8_t *pattern,
                                           size_t len,
                                           uint32_t **out_indices,
                                           uint32_t *out_count);
bool cetta_mork_bridge_space_query_bindings(CettaMorkSpaceHandle *space,
                                            const uint8_t *pattern,
                                            size_t len,
                                            uint8_t **out_packet,
                                            size_t *out_len,
                                            uint32_t *out_rows);
bool cetta_mork_bridge_space_query_bindings_query_only_v2(CettaMorkSpaceHandle *space,
                                                          const uint8_t *pattern,
                                                          size_t len,
                                                          uint8_t **out_packet,
                                                          size_t *out_len,
                                                          uint32_t *out_rows);
bool cetta_mork_bridge_space_query_bindings_multi_ref_v3(CettaMorkSpaceHandle *space,
                                                         const uint8_t *pattern,
                                                         size_t len,
                                                         uint8_t **out_packet,
                                                         size_t *out_len,
                                                         uint32_t *out_rows);
void cetta_mork_bridge_bytes_free(uint8_t *data, size_t len);

#endif /* CETTA_MORK_SPACE_BRIDGE_RUNTIME_H */
