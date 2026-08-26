#!/usr/bin/env bash
# Regenerate the exact-source Nil chaining benchmarks from a pinned Git object.
# OBC/OBFC engine definitions are stored once; the benchmark runner composes
# them with individual queries. Standalone IC programs are stored once each.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/benchmarks/chaining/nil_current"
ENGINES="$OUT/engines"
QUERIES="$OUT/queries"
PROGRAMS="$OUT/programs"
SOURCE_REV="2151416f4d44b3b2dbbcd57a0dd7ffd5a75c992c"
OBC_PATH="experimental/backward-via-forward/obc-xp.metta"
OBFC_PATH="experimental/backward-via-forward/obfc-xp.metta"
IC_DEDUCTION_PATH="experimental/inference-control/inf-ctl-xp.metta"
IC_CALENDAR_PATH="experimental/inference-control/inf-ctl-month-xp.metta"
IC_NESTED_CONTROL_PATH="experimental/inference-control/inf-ctl-month-bc-xp.metta"
POLYWARD_PATH="experimental/polyward-chaining/pc-xp.metta"
OBC_BLOB_SHA256="f502c32358a54b96eafa6bf0515aa51277b18e88b216f30e42e7b827f3a5fae7"
OBFC_BLOB_SHA256="eddac8b756f12757881cf25fd75932e016d1012bb918c58ecce17fc47a46b764"
IC_DEDUCTION_BLOB_SHA256="88545ab5d14eac859d0cac3d184ca4540126e5bbf42892088a2bc3b00a1caa77"
IC_CALENDAR_BLOB_SHA256="899565a571d9e70921ac24aa02f65e1f21e4e2b8c8993e4eba3cdc895f65ed1b"
IC_NESTED_CONTROL_BLOB_SHA256="7fda0498ae3f076f6ef7a5d9aed02e9ca3d7b9504451d6946313bfa632265c81"
POLYWARD_BLOB_SHA256="83c6a2e1a9318ffba43a8d2d6275991e8ad32cf40c0af6956fd40f8b570b7ab3"
: "${CHAINING_SOURCE_ROOT:?set CHAINING_SOURCE_ROOT to a Git checkout containing the pinned Nil revision}"

export LC_ALL=C.UTF-8
mkdir -p "$ROOT/runtime" "$ENGINES" "$QUERIES" "$PROGRAMS"
scratch="$(mktemp -d "$ROOT/runtime/nil-chaining-source.XXXXXX")"
trap 'rm -rf "$scratch"' EXIT

if ! git -C "$CHAINING_SOURCE_ROOT" cat-file -e "$SOURCE_REV^{commit}"; then
    printf 'pinned Nil revision is unavailable: %s\n' "$SOURCE_REV" >&2
    exit 2
fi

obc_source="$scratch/obc-xp.metta"
obfc_source="$scratch/obfc-xp.metta"
ic_deduction_source="$scratch/inf-ctl-xp.metta"
ic_calendar_source="$scratch/inf-ctl-month-xp.metta"
ic_nested_control_source="$scratch/inf-ctl-month-bc-xp.metta"
polyward_source="$scratch/pc-xp.metta"
git -C "$CHAINING_SOURCE_ROOT" show "$SOURCE_REV:$OBC_PATH" >"$obc_source"
git -C "$CHAINING_SOURCE_ROOT" show "$SOURCE_REV:$OBFC_PATH" >"$obfc_source"
git -C "$CHAINING_SOURCE_ROOT" show "$SOURCE_REV:$IC_DEDUCTION_PATH" >"$ic_deduction_source"
git -C "$CHAINING_SOURCE_ROOT" show "$SOURCE_REV:$IC_CALENDAR_PATH" >"$ic_calendar_source"
git -C "$CHAINING_SOURCE_ROOT" show "$SOURCE_REV:$IC_NESTED_CONTROL_PATH" >"$ic_nested_control_source"
git -C "$CHAINING_SOURCE_ROOT" show "$SOURCE_REV:$POLYWARD_PATH" >"$polyward_source"

verify_sha256() {
    local file="$1" expected="$2" label="$3" actual
    actual="$(sha256sum "$file" | awk '{print $1}')"
    if [[ "$actual" != "$expected" ]]; then
        printf '%s blob SHA-256 mismatch: expected %s, got %s\n' \
            "$label" "$expected" "$actual" >&2
        exit 2
    fi
}
verify_sha256 "$obc_source" "$OBC_BLOB_SHA256" obc-xp.metta
verify_sha256 "$obfc_source" "$OBFC_BLOB_SHA256" obfc-xp.metta
verify_sha256 "$ic_deduction_source" "$IC_DEDUCTION_BLOB_SHA256" inf-ctl-xp.metta
verify_sha256 "$ic_calendar_source" "$IC_CALENDAR_BLOB_SHA256" inf-ctl-month-xp.metta
verify_sha256 "$ic_nested_control_source" "$IC_NESTED_CONTROL_BLOB_SHA256" inf-ctl-month-bc-xp.metta
verify_sha256 "$polyward_source" "$POLYWARD_BLOB_SHA256" pc-xp.metta

require_unique_marker() {
    local source="$1" marker="$2" count
    count="$(grep -Fxc -- "$marker" "$source" || true)"
    if [[ "$count" != 1 ]]; then
        printf 'expected one structural marker %q in %s, found %s\n' \
            "$marker" "$source" "$count" >&2
        exit 2
    fi
}

extract_before_marker() {
    local source="$1" marker="$2"
    require_unique_marker "$source" "$marker"
    awk -v marker="$marker" '$0 == marker { exit } { print }' "$source"
}

extract_after_marker() {
    local source="$1" marker="$2"
    require_unique_marker "$source" "$marker"
    awk -v marker="$marker" '
        active { print }
        $0 == marker { active = 1 }
    ' "$source"
}

extract_section() {
    local source="$1" begin="$2" end="$3"
    require_unique_marker "$source" "$begin"
    require_unique_marker "$source" "$end"
    awk -v begin="$begin" -v end="$end" '
        $0 == begin { active = 1 }
        $0 == end { exit }
        active { print }
    ' "$source"
}

extract_named_query() {
    local source="$1" predicate="$2" marker="$3"
    awk -v marker="$marker" -v predicate="$predicate" '
        function is_header(line, prefix, next_char) {
            if (index(line, prefix) != 1) return 0
            if (length(line) == length(prefix)) return 1
            next_char = substr(line, length(prefix) + 1, 1)
            return next_char == " " || next_char == "("
        }
        is_header($0, ";; ;; " marker) || is_header($0, ";; " marker) {
            headers++
            seeking = 1
            next
        }
        seeking && (index($0, ";; !(" predicate " ") == 1 ||
                    index($0, "!(" predicate " ") == 1) {
            line = $0
            sub(/^;; /, "", line)
            queries++
            seeking = 0
        }
        END {
            if (headers != 1 || queries != 1) exit 42
            print line
        }
    ' "$source" || {
        printf 'could not extract unique %s query for marker %s\n' \
            "$predicate" "$marker" >&2
        exit 2
    }
}

extract_enumeration_query() {
    local source="$1" predicate="$2" bound="$3" target
    target=";; !($predicate $bound (: \$x \$a))"
    awk -v target="$target" '
        index($0, target) == 1 { matches++ }
        END {
            if (matches != 1) exit 42
            line = target
            sub(/^;; /, "", line)
            print line
        }
    ' "$source" || {
        printf 'expected one enumeration query beginning with %q\n' \
            "$target" >&2
        exit 2
    }
}

set_bound() {
    local query="$1" predicate="$2" bound="$3" rewritten
    rewritten="$(printf '%s\n' "$query" | sed -E \
        "s/^!\\($predicate [0-9]+ /!($predicate $bound /")"
    if [[ "$rewritten" != "!($predicate $bound "* ]]; then
        printf 'failed to set %s bound to %s in %s\n' \
            "$predicate" "$bound" "$query" >&2
        exit 2
    fi
    printf '%s\n' "$rewritten"
}

obc_engine="$scratch/obc.engine.metta"
obfc_engine="$scratch/obfc.engine.metta"
obfc_queries="$scratch/obfc.queries.metta"
extract_before_marker "$obc_source" \
    ';; Test proof-sized bound optimized backward chainer ;;' \
    >"$obc_engine"
{
    extract_before_marker "$obfc_source" ';; Reduction ;;'
    printf '\n'
    extract_section "$obfc_source" \
        ';; Optimized Forward Chainer ;;' ';; ;; Main ;; ;;'
} >"$obfc_engine"
extract_after_marker "$obfc_source" ';; ;; Main ;; ;;' >"$obfc_queries"

for engine in "$obc_engine" "$obfc_engine"; do
    occurs_count="$(grep -Fxc \
        '!(translatePredicate (set_prolog_flag occurs_check True))' \
        "$engine" || true)"
    if [[ "$occurs_count" != 1 ]]; then
        printf 'engine slice must contain exactly one occurs-check setup: %s\n' \
            "$engine" >&2
        exit 2
    fi
done

write_engine() {
    local name="$1" source_path="$2" source_sha="$3" marker="$4" source="$5"
    local target="$ENGINES/$name" semantic_sha artifact_sha
    semantic_sha="$(sha256sum "$source" | awk '{print $1}')"
    {
        printf ';; Generated file; do not edit.\n'
        printf ';; Nil source revision: %s\n' "$SOURCE_REV"
        printf ';; Nil source path: %s\n' "$source_path"
        printf ';; Source blob SHA-256: %s\n' "$source_sha"
        printf ';; Structural slice: %s\n\n' "$marker"
        sed -n '1,$p' "$source"
    } >"$target"
    artifact_sha="$(sha256sum "$target" | awk '{print $1}')"
    printf '%s\tengine\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "engines/$name" "$SOURCE_REV" "$source_path" "$source_sha" \
        "$marker" "$semantic_sha" "$artifact_sha" >>"$provenance"
    printf 'wrote %s\n' "$target"
}

write_query() {
    local name="$1" source_path="$2" source_sha="$3" marker="$4" query="$5"
    local target="$QUERIES/$name" query_sha artifact_sha
    query_sha="$(printf '%s\n' "$query" | sha256sum | awk '{print $1}')"
    {
        printf ';; Generated file; do not edit.\n'
        printf ';; Nil source revision: %s\n' "$SOURCE_REV"
        printf ';; Nil source path: %s\n' "$source_path"
        printf ';; Source blob SHA-256: %s\n' "$source_sha"
        printf ';; Query marker: %s\n\n' "$marker"
        printf '%s\n' "$query"
    } >"$target"
    artifact_sha="$(sha256sum "$target" | awk '{print $1}')"
    printf '%s\tquery\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "queries/$name" "$SOURCE_REV" "$source_path" "$source_sha" \
        "$marker" "$query_sha" "$artifact_sha" >>"$provenance"
    printf 'wrote %s\n' "$target"
}

write_program() {
    local name="$1" source_path="$2" source_sha="$3" projection="$4" source="$5"
    local target="$PROGRAMS/$name" semantic_sha artifact_sha
    semantic_sha="$(sha256sum "$source" | awk '{print $1}')"
    {
        printf ';; Generated file; do not edit.\n'
        printf ';; Nil source revision: %s\n' "$SOURCE_REV"
        printf ';; Nil source path: %s\n' "$source_path"
        printf ';; Source blob SHA-256: %s\n' "$source_sha"
        printf ';; Projection: %s\n\n' "$projection"
        sed -n '1,$p' "$source"
    } >"$target"
    artifact_sha="$(sha256sum "$target" | awk '{print $1}')"
    printf '%s\tprogram\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "programs/$name" "$SOURCE_REV" "$source_path" "$source_sha" \
        "$projection" "$semantic_sha" "$artifact_sha" >>"$provenance"
    printf 'wrote %s\n' "$target"
}

provenance="$OUT/source_provenance.tsv"
printf 'artifact\tkind\tsource_revision\tsource_path\tsource_blob_sha256\tstructural_marker\tsemantic_sha256\tartifact_sha256\n' \
    >"$provenance"

write_engine obc.metta "$OBC_PATH" "$OBC_BLOB_SHA256" \
    'before: Test proof-sized bound optimized backward chainer' "$obc_engine"
write_engine obfc.metta "$OBFC_PATH" "$OBFC_BLOB_SHA256" \
    'before: Reduction + section: Optimized Forward Chainer..Main' "$obfc_engine"

jarr="$(extract_named_query "$obc_source" obc jarr)"
loowoz="$(extract_named_query "$obc_source" obc loowoz)"
pm227="$(extract_named_query "$obc_source" obc pm2.27)"
imim1="$(extract_named_query "$obc_source" obc imim1)"
enum13="$(extract_enumeration_query "$obc_source" obc 13)"
enum15="$(extract_enumeration_query "$obc_source" obc 15)"

write_query jarr_s13.metta "$OBC_PATH" "$OBC_BLOB_SHA256" \
    jarr "$(set_bound "$jarr" obc 13)"
write_query loowoz_s19.metta "$OBC_PATH" "$OBC_BLOB_SHA256" \
    loowoz "$(set_bound "$loowoz" obc 19)"
write_query all_s13.metta "$OBC_PATH" "$OBC_BLOB_SHA256" \
    enumerate-13 "$enum13"
write_query all_s15.metta "$OBC_PATH" "$OBC_BLOB_SHA256" \
    enumerate-15 "$enum15"

deep_query="$(printf '%s\n%s\n%s' \
    "$(set_bound "$jarr" obc 15)" \
    "$(set_bound "$pm227" obc 15)" \
    "$(set_bound "$imim1" obc 17)")"
write_query deep_triple_15_17.metta "$OBC_PATH" "$OBC_BLOB_SHA256" \
    jarr+pm2.27+imim1 "$deep_query"

obfc_jarr="$(extract_named_query "$obfc_queries" obfc jarr)"
obfc_loowoz="$(extract_named_query "$obfc_queries" obfc loowoz)"
write_query obfc_jarr_d13.metta "$OBFC_PATH" "$OBFC_BLOB_SHA256" jarr \
    "$(set_bound "$obfc_jarr" obfc 13)"
write_query obfc_loowoz_d19.metta "$OBFC_PATH" "$OBFC_BLOB_SHA256" loowoz \
    "$(set_bound "$obfc_loowoz" obfc 19)"

polyward_portable="$scratch/polyward-portable.metta"
polyward_banner_count="$(grep -Ec '^! \".*\"$' "$polyward_source" || true)"
if [[ "$polyward_banner_count" != 1 ]]; then
    printf 'expected one standalone polyward banner, found %s\n' \
        "$polyward_banner_count" >&2
    exit 2
fi
sed '/^! \".*\"$/d' "$polyward_source" >"$polyward_portable"

write_program ic_deduction.metta "$IC_DEDUCTION_PATH" \
    "$IC_DEDUCTION_BLOB_SHA256" whole-source "$ic_deduction_source"
write_program ic_calendar.metta "$IC_CALENDAR_PATH" \
    "$IC_CALENDAR_BLOB_SHA256" whole-source "$ic_calendar_source"
write_program ic_nested_control.metta "$IC_NESTED_CONTROL_PATH" \
    "$IC_NESTED_CONTROL_BLOB_SHA256" whole-source "$ic_nested_control_source"
write_program polyward.metta "$POLYWARD_PATH" "$POLYWARD_BLOB_SHA256" \
    'whole-source-minus-one-standalone-banner' "$polyward_portable"

printf 'pinned Nil revision %s\n' "$SOURCE_REV"
printf 'obc engine slice SHA-256 %s\n' "$(sha256sum "$obc_engine" | awk '{print $1}')"
printf 'obfc engine slice SHA-256 %s\n' "$(sha256sum "$obfc_engine" | awk '{print $1}')"
