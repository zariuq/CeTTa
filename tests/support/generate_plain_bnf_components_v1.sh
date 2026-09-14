#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
    echo 'usage: generate_plain_bnf_components_v1.sh COMPILER OUTPUT' >&2
    exit 2
fi
components_compiler=$(realpath "$1")
components_output=$(realpath -m "$2")
components_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
cd "$components_root"
"$components_compiler" petta-direct \
    --source experiments/gslt2parse_foundation/presentations/shared/ground_relations_v1.metta \
    --source experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_relations_v1.metta \
    --source experiments/gslt2parse_foundation/presentations/shared/ground_integer_relations_v1.metta \
    --source experiments/gslt2parse_foundation/presentations/shared/cetta_petta_ground_integer_relations_v1.metta \
    --source langdef/bnf/plain_bnf_semantic_admission_v1.metta \
    --source langdef/bnf/plain_bnf_graph_analysis_v1.metta \
    --source langdef/bnf/plain_bnf_graph_index_v1.metta \
    --source langdef/bnf/plain_bnf_graph_discovery_v1.metta \
    --closed-entry-mode BNFGraphTrieLookupV1:110 \
    --closed-entry-mode BNFGraphTrieInsertFirstV1:1110 \
    --closed-entry-mode BNFDiscoveryReverseDefinitionsV1:110 \
    --closed-entry-mode BNFDiscoveryCollectDefinitionsV1:1000 \
    --out "$components_output"
