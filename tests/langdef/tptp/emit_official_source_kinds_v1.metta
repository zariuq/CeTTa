!(import! &self lib/lib_tptp.metta)
!(import! &self system)

(= (source-kinds:rows (tptp-ebnf-v1:entries-nil))
   (SourceRowsNilV1))
(= (source-kinds:rows
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:syntax-row $name $_rhs $span) $tail))
   (SourceRowsConsV1 (SourceRowV1 syntax $name $span)
     (source-kinds:rows $tail)))
(= (source-kinds:rows
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:semantic-row $name $_rhs $span) $tail))
   (SourceRowsConsV1 (SourceRowV1 semantic $name $span)
     (source-kinds:rows $tail)))
(= (source-kinds:rows
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:token-row $name $_rhs $span) $tail))
   (SourceRowsConsV1 (SourceRowV1 token $name $span)
     (source-kinds:rows $tail)))
(= (source-kinds:rows
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:character-row $name $_rhs $span) $tail))
   (SourceRowsConsV1 (SourceRowV1 character $name $span)
     (source-kinds:rows $tail)))
(= (source-kinds:rows
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:comment $_text $span) $tail))
   (SourceRowsConsV1 (SourceCommentV1 $span)
     (source-kinds:rows $tail)))
(= (source-kinds:rows
      (tptp-ebnf-v1:entries-cons
        (tptp-ebnf-v1:blank $span) $tail))
   (SourceRowsConsV1 (SourceBlankV1 $span)
     (source-kinds:rows $tail)))

(= (source-kinds:emit
      (TPTP:StageValue extended-bnf-ast
        (TPTP:ExtendedBnfAst $_binding $digest
          (tptp-ebnf-v1:document $entries $_span))) $path)
   (tptp:write-atom $path
     (TptpOfficialSourceKindsV1 $digest (source-kinds:rows $entries))))

!(source-kinds:emit
   (tptp:extended-bnf-file->ast (system:arg 0))
   "tests/langdef/tptp/kimi_tmp/source_kind_ledger_v1.metta")
