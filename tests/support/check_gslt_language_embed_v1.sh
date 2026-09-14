#!/usr/bin/env bash
set -euo pipefail
if (( $# != 2 )); then
    echo 'usage: check_gslt_language_embed_v1.sh GENERATOR NEW_EVIDENCE_DIRECTORY' >&2
    exit 2
fi
tool=$(realpath "$1")
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
mkdir -p "$2"
evidence=$(realpath "$2")
test ! -e "$evidence/inputs.sha256"
cd "$repo"
manifests=(petta/typecheck_v3_core_runtime_v1 gslt-il/langdef metta-interact/langdef
           subzero/langdef zero/langdef zero/langdef zero/langdef zero/langdef zerouv/langdef)
profiles=('' '' '' '' '' exp emit interact '')
stems=(petta_typecheck_v3_core_v1 gslt_il_language_v1 metta_interact_language_v1
       subzero_language_v1 zero_language_v1 zero_exp_language_v1 zero_emit_language_v1
       zero_interact_language_v1 zerouv_language_v1)
sha256sum "$tool" "$0" tools/gslt_language_embed_v1.c \
    tests/support/test_gslt_language_embed_descriptor_v1.c > "$evidence/inputs.sha256"
tool_sha=$(sha256sum "$tool"); tool_sha=${tool_sha%% *}
cc_bin=${CC:-cc}
read -r -a flags <<< "${CFLAGS:--O2 -Wall -Wextra -Werror}"
flags+=(-std=c11 -pthread -I. -Isrc -DCETTA_BUILD_WITH_GMP=0 -DCETTA_BUILD_WITH_RUNTIME_STATS=0)
"$cc_bin" "${flags[@]}" -c src/native_sha256.c -o "$evidence/sha.o"
for i in "${!stems[@]}"; do
    stem=${stems[$i]}
    dir="$evidence/$stem"
    mkdir -p "$dir/first" "$dir/second"
    profile=()
    source_root=langdef
    if [[ $stem == subzero_language_v1 ]]; then source_root=langdef/subzero; fi
    if [[ -n ${profiles[$i]} ]]; then profile=(--profile "${profiles[$i]}"); fi
    sha256sum "langdef/${manifests[$i]}.metta" "src/generated/$stem.generated.c" \
        "src/generated/$stem.generated.h" >> "$evidence/inputs.sha256"
    for trial in first second; do
        "$tool" --manifest "langdef/${manifests[$i]}.metta" --source-root "$source_root" \
            "${profile[@]}" --symbol "cetta_$stem" --header "$dir/$trial/native.h" \
            --source "$dir/$trial/native.c" --header-include native.h
    done
    cmp "$dir/first/native.h" "$dir/second/native.h"
    cmp "$dir/first/native.c" "$dir/second/native.c"
    "$cc_bin" "${flags[@]}" -D"cetta_$stem=candidate" -c "$dir/first/native.c" -o "$dir/candidate.o"
    "$cc_bin" "${flags[@]}" -D"cetta_$stem=reference" -c "src/generated/$stem.generated.c" -o "$dir/reference.o"
    "$cc_bin" "${flags[@]}" -DCANDIDATE_DESCRIPTOR=candidate -DREFERENCE_DESCRIPTOR=reference \
        tests/support/test_gslt_language_embed_descriptor_v1.c "$dir/candidate.o" \
        "$dir/reference.o" "$evidence/sha.o" -o "$dir/compare"
    "$dir/compare" "$tool_sha"
done
sha256sum -c "$evidence/inputs.sha256" > "$evidence/inputs-after.txt"
echo 'GsltLanguageNativeEmbedV1: 9 exact descriptors; 9 deterministic regenerations'
