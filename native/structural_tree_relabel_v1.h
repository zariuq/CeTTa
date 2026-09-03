#ifndef CETTA_STRUCTURAL_TREE_RELABEL_V1_H
#define CETTA_STRUCTURAL_TREE_RELABEL_V1_H

#include "src/atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaStructuralTreeRelabelV1 CettaStructuralTreeRelabelV1;

typedef enum {
    CETTA_TREE_RELABEL_V1_OK = 0,
    CETTA_TREE_RELABEL_V1_BAD_ARGUMENT,
    CETTA_TREE_RELABEL_V1_INVALID_PRESENTATION,
    CETTA_TREE_RELABEL_V1_UNSUPPORTED_RULE,
    CETTA_TREE_RELABEL_V1_AMBIGUOUS_RULE,
    CETTA_TREE_RELABEL_V1_UNKNOWN_CONSTRUCTOR,
    CETTA_TREE_RELABEL_V1_UNKNOWN_LABEL,
    CETTA_TREE_RELABEL_V1_NON_GROUND_TERM,
    CETTA_TREE_RELABEL_V1_RESOURCE_LIMIT
} CettaStructuralTreeRelabelV1Status;

/*
 * Compile one left-linear first-order tree relabeler from ordered authored
 * GSLT equation presentations.  Entry equations may copy a source child,
 * recurse through the named entry operator, or relabel a nullary child through
 * the named label operator.  No guest-language constructor names are built in.
 * The one-path entry point below is the common single-presentation case.
 */
bool cetta_structural_tree_relabel_v1_load_paths(
    const char *const *presentation_paths,
    size_t presentation_path_count,
    const char *entry_operator,
    const char *label_operator,
    CettaStructuralTreeRelabelV1 **out,
    CettaStructuralTreeRelabelV1Status *status,
    char *error_buf,
    size_t error_buf_size);

bool cetta_structural_tree_relabel_v1_load(
    const char *presentation_path,
    const char *entry_operator,
    const char *label_operator,
    CettaStructuralTreeRelabelV1 **out,
    CettaStructuralTreeRelabelV1Status *status,
    char *error_buf,
    size_t error_buf_size);

void cetta_structural_tree_relabel_v1_free(
    CettaStructuralTreeRelabelV1 *plan);

bool cetta_structural_tree_relabel_v1_apply(
    const CettaStructuralTreeRelabelV1 *plan,
    const Atom *source,
    Arena *arena,
    uint32_t depth_limit,
    uint64_t work_limit,
    Atom **out,
    CettaStructuralTreeRelabelV1Status *status,
    char *error_buf,
    size_t error_buf_size);

const char *cetta_structural_tree_relabel_v1_status_name(
    CettaStructuralTreeRelabelV1Status status);

#endif /* CETTA_STRUCTURAL_TREE_RELABEL_V1_H */
