#!/usr/bin/env bash
set -euo pipefail
if (( $# != 3 )); then
    echo 'usage: check_tptp_reader_gate_failure_v1.sh EVIDENCE_DIRECTORY SYNTAX_BNF CORPUS_ROOT' >&2
    exit 2
fi
reader_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
mkdir -p "$1"
reader_evidence=$(realpath "$1")
reader_syntax=$(realpath "$2")
reader_corpus=$(realpath "$3")
test ! -e "$reader_evidence/lib"
mkdir -p "$reader_evidence/tests/langdef/tptp"
ln -s "$reader_root/lib" "$reader_evidence/lib"
cd "$reader_root"

# Keep the ordinary relative imports and actual compiled transformations.
# Change one expected result, not the recognizer or its input.
awk '
  { count += sub(/\(TestCase "cat_42" accepted\)/,
                 "(TestCase \"cat_42\" rejected)"); print }
  END { if (count != 1) exit 1 }
' tests/langdef/tptp/official_lexical_to_ebnf_v1.metta \
  > "$reader_evidence/tests/langdef/tptp/lexical.metta"
awk '
  { count += sub(/\(SyntaxCase "" accepted\)/,
                 "(SyntaxCase \"\" rejected)"); print }
  END { if (count != 1) exit 1 }
' tests/langdef/tptp/official_syntax_composition_v1.metta \
  > "$reader_evidence/tests/langdef/tptp/syntax.metta"
awk '
  { count += sub(/\(ReadCase "" LNil\)/,
                 "(ReadCase \"\" (LCons (Token \"lower_word\" \"unexpected\") LNil))"); print }
  END { if (count != 1) exit 1 }
' tests/langdef/tptp/official_syntax_observation_v1.metta \
  > "$reader_evidence/tests/langdef/tptp/observation.metta"
awk '
  { count += sub(/\(RecordCase ""/,
                 "(RecordCase \"fof(a,axiom,p).\""); print }
  END { if (count != 1) exit 1 }
' tests/langdef/tptp/official_syntax_records_v1.metta \
  > "$reader_evidence/tests/langdef/tptp/records.metta"

if make --no-print-directory \
    TPTP_OFFICIAL_SYNTAX_BNF_V1="$reader_syntax" \
    TPTP_OFFICIAL_LEXICAL_TEST_V1="$reader_evidence/tests/langdef/tptp/lexical.metta" \
    test-tptp-official-lexical-to-ebnf-v1 > "$reader_evidence/lexical.log" 2>&1; then
    echo 'lexical gate accepted an intentionally wrong expectation' >&2
    exit 1
fi
rg -q 'TptpLexicalMismatch "cat_42" rejected accepted' "$reader_evidence/lexical.log"

if make --no-print-directory \
    TPTP_OFFICIAL_SYNTAX_BNF_V1="$reader_syntax" \
    TPTP_OFFICIAL_CORPUS_ROOT_V1="$reader_corpus" \
    TPTP_OFFICIAL_SYNTAX_COMPOSITION_TEST_V1="$reader_evidence/tests/langdef/tptp/syntax.metta" \
    test-tptp-official-syntax-composition-v1 > "$reader_evidence/syntax.log" 2>&1; then
    echo 'syntax gate accepted an intentionally wrong expectation' >&2
    exit 1
fi
rg -q 'TptpSyntaxMismatch "" rejected accepted' "$reader_evidence/syntax.log"

if make --no-print-directory \
    TPTP_OFFICIAL_SYNTAX_BNF_V1="$reader_syntax" \
    TPTP_OFFICIAL_SYNTAX_OBSERVATION_TEST_V1="$reader_evidence/tests/langdef/tptp/observation.metta" \
    TPTP_OFFICIAL_CORPUS_ROOT_V1="$reader_corpus" \
    test-tptp-official-syntax-observation-v1 > "$reader_evidence/observation.log" 2>&1; then
    echo 'observation gate accepted an intentionally wrong expectation' >&2
    exit 1
fi
rg -q 'TptpObservationMismatch ""' "$reader_evidence/observation.log"

if make --no-print-directory \
    TPTP_OFFICIAL_SYNTAX_BNF_V1="$reader_syntax" \
    TPTP_OFFICIAL_SYNTAX_RECORDS_TEST_V1="$reader_evidence/tests/langdef/tptp/records.metta" \
    TPTP_OFFICIAL_CORPUS_ROOT_V1="$reader_corpus" \
    test-tptp-official-syntax-records-v1 > "$reader_evidence/records.log" 2>&1; then
    echo 'records gate accepted an intentionally wrong expectation' >&2
    exit 1
fi
rg -q 'TptpRecordMismatch' "$reader_evidence/records.log"

awk '
  { count += sub(/\(thf mix_type type/,
                 "(thf mix_type-wrong type"); print }
  END { if (count != 1) exit 1 }
' tests/langdef/tptp/official_syntax_compact_v1.expected \
  > "$reader_evidence/tests/langdef/tptp/compact.expected"
if make --no-print-directory \
    TPTP_OFFICIAL_SYNTAX_BNF_V1="$reader_syntax" \
    TPTP_OFFICIAL_CORPUS_ROOT_V1="$reader_corpus" \
    TPTP_OFFICIAL_SYNTAX_COMPACT_EXPECTED_V1="$reader_evidence/tests/langdef/tptp/compact.expected" \
    test-tptp-official-syntax-compact-v1 > "$reader_evidence/compact.log" 2>&1; then
    echo 'compact gate accepted an intentionally wrong expectation' >&2
    exit 1
fi
rg -q '\(thf mix_type type \(: mix \(⇒ beverage beverage\)\)\)' \
  "$reader_evidence/compact.log"
echo '(TptpReaderGateFailureV1Summary 5 0)'
