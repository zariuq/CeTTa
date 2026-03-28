#ifndef CETTA_MORK_SPACE_BRIDGE_RUNTIME_H
#define CETTA_MORK_SPACE_BRIDGE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaMorkSpaceHandle CettaMorkSpaceHandle;
typedef struct CettaMorkProgramHandle CettaMorkProgramHandle;
typedef struct CettaMorkContextHandle CettaMorkContextHandle;

bool cetta_mork_bridge_is_available(void);
const char *cetta_mork_bridge_last_error(void);

CettaMorkSpaceHandle *cetta_mork_bridge_space_new(void);
void cetta_mork_bridge_space_free(CettaMorkSpaceHandle *space);

bool cetta_mork_bridge_space_clear(CettaMorkSpaceHandle *space);
bool cetta_mork_bridge_space_add_indexed_sexpr(CettaMorkSpaceHandle *space,
                                               uint32_t atom_idx,
                                               const uint8_t *text,
                                               size_t len);
bool cetta_mork_bridge_space_size(const CettaMorkSpaceHandle *space,
                                  uint64_t *out_size);
bool cetta_mork_bridge_space_dump(CettaMorkSpaceHandle *space,
                                  uint8_t **out_packet,
                                  size_t *out_len,
                                  uint32_t *out_rows);
bool cetta_mork_bridge_space_dump_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_saved);
bool cetta_mork_bridge_space_load_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_loaded);
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

CettaMorkProgramHandle *cetta_mork_bridge_program_new(void);
void cetta_mork_bridge_program_free(CettaMorkProgramHandle *program);
bool cetta_mork_bridge_program_clear(CettaMorkProgramHandle *program);
bool cetta_mork_bridge_program_add_sexpr(CettaMorkProgramHandle *program,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added);
bool cetta_mork_bridge_program_size(const CettaMorkProgramHandle *program,
                                    uint64_t *out_size);
bool cetta_mork_bridge_program_dump(CettaMorkProgramHandle *program,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows);

CettaMorkContextHandle *cetta_mork_bridge_context_new(void);
void cetta_mork_bridge_context_free(CettaMorkContextHandle *context);
bool cetta_mork_bridge_context_clear(CettaMorkContextHandle *context);
bool cetta_mork_bridge_context_load_program(CettaMorkContextHandle *context,
                                            const CettaMorkProgramHandle *program,
                                            uint64_t *out_added);
bool cetta_mork_bridge_context_add_sexpr(CettaMorkContextHandle *context,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added);
bool cetta_mork_bridge_context_remove_sexpr(CettaMorkContextHandle *context,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed);
bool cetta_mork_bridge_context_size(const CettaMorkContextHandle *context,
                                    uint64_t *out_size);
bool cetta_mork_bridge_context_run(CettaMorkContextHandle *context,
                                   uint64_t steps, uint64_t *out_performed);
bool cetta_mork_bridge_context_dump(CettaMorkContextHandle *context,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows);

void cetta_mork_bridge_bytes_free(uint8_t *data, size_t len);

#endif /* CETTA_MORK_SPACE_BRIDGE_RUNTIME_H */
