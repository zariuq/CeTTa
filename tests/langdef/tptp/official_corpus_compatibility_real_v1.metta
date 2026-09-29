; Manual scale probe for the generic authored oracle.  This is deliberately
; not a release gate: large files belong on a separately frozen compatibility
; snapshot so the reader does not retain generic derivation structure.
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self ../../../langdef/tptp/official_prepared_reader_v1.metta)
!(import! &self system)

(= (compat-real:parser
     (TPTP:CorpusCompatibleGrammarV1 $source
       (bnf-v1:grammar-input $document $authority)))
   ($source (bnf:parser-from-ebnf-document $document $authority)))
(= (compat-real:classification $outcome)
   (case $outcome
     (((EBNF:Parse (EBNF:Accepted $_binding $_source $_trees) $_receipt)
        accepted)
      ((EBNF:Parse
         (EBNF:Rejected $_binding $_source $_offset $_expected) $_receipt)
        rejected)
      ($_ unexpected))))
(= (compat-real:use $source (EBNF:Parser $handle $receipt) $path)
   (let $parser (EBNF:Parser $handle $receipt)
     (let $classification
          (compat-real:classification (bnf:parse-ebnf-file $parser $path))
       (let $_ (bnf:close-ebnf-parser $parser)
       (TptpOfficialCorpusCompatibilityRealV1Summary
           $source $classification)))))

!(let ($source $document)
      (tptp-prep:document
        (tptp:extended-bnf-file->ast (system:arg 0)))
   (let ($profile-source $parser)
        (compat-real:parser
          (tptp-prep:compose-corpus-compatible $source $document))
     (compat-real:use $profile-source $parser (system:arg 1))))
