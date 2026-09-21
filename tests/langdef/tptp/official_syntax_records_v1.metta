!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self system)

(= (record-test:document
     (TPTP:StageValue extended-bnf-ast (TPTP:ExtendedBnfAst $_binding $source $document)))
   ($source $document))
(= (record-test:one-root $result)
   (case $result
     (((LCons (LCons $root LNil) LNil) $root)
      ($fault (TptpRecordRootFailure $fault)))))
(= (record-test:observe $program $context (EBNF:Parser $handle $receipt) $text)
   (case (langdef:parse-authored-text $handle $text)
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
       (langdef:run-deterministic-equations $program
         (LangDef:DeterministicCall tptp-rec-v1:node
           (record-test:one-root
             (langdef:run-deterministic-equations $program
               (LangDef:DeterministicCall tptp-read-v1:roots $context
                 (bnf:project-ebnf-trees $receipt $trees)))))))
      ($fault (TptpRecordParseFailure $fault)))))
(= (record-test:name-text (tptp-rec:name-word (tptp-rec:word-lower $text))) $text)
(= (record-test:name-text (tptp-rec:name-word (tptp-rec:word-quoted $text))) $text)
(= (record-test:input-name (tptp-rec:fof $name $_role $_formula $_ann $_span))
   (record-test:name-text $name))
(= (record-test:input-name (tptp-rec:cnf $name $_role $_formula $_ann $_span))
   (record-test:name-text $name))
(= (record-test:input-name (tptp-rec:tff $name $_role $_formula $_ann $_span))
   (record-test:name-text $name))
(= (record-test:input-name (tptp-rec:thf $name $_role $_formula $_ann $_span))
   (record-test:name-text $name))
(= (record-test:input-name (tptp-rec:tcf $name $_role $_formula $_ann $_span))
   (record-test:name-text $name))
(= (record-test:input-name (tptp-rec:tpi $name $_role $_formula $_ann $_span))
   (record-test:name-text $name))
(= (record-test:names (tptp-rec:file $inputs $_span))
   (record-test:input-names $inputs))
(= (record-test:input-names (tptp-rec:inputs-nil)) LNil)
(= (record-test:input-names (tptp-rec:inputs-cons $head $tail))
   (LCons (record-test:input-name $head) (record-test:input-names $tail)))
(= (record-test:first-input (tptp-rec:file (tptp-rec:inputs-cons $head $_tail) $_span)) $head)
(= (record-test:cnf-formula (tptp-rec:cnf $_name $_role $formula $_ann $_span)) $formula)
(= (record-test:fof-formula (tptp-rec:fof $_name $_role $formula $_ann $_span)) $formula)
(= (record-test:thf-formula (tptp-rec:thf $_name $_role $formula $_ann $_span)) $formula)
(= (record-test:tff-formula (tptp-rec:tff $_name $_role $formula $_ann $_span)) $formula)
(= (record-test:include-sel (tptp-rec:include $_file $selection $_qual $_span)) $selection)
(= (record-test:cases $program $context $parser LNil) 0)
(= (record-test:cases $program $context $parser (LCons (RecordCase $text $expected) $rest))
   (let $actual (record-test:observe $program $context $parser $text)
     (case (== $actual $expected)
       ((True (+ 1 (record-test:cases $program $context $parser $rest)))
        (False (TptpRecordMismatch $text $expected $actual))))))
(= (record-test:checks $program $context $parser)
   (record-test:cases $program $context $parser
     (LCons (RecordCase ""
       (tptp-rec:file (tptp-rec:inputs-nil) (tptp-rec:span 0 0)))
     (LCons (RecordCase "fof(a,axiom,p)."
       (tptp-rec:file
         (tptp-rec:inputs-cons
           (tptp-rec:fof
             (tptp-rec:name-word (tptp-rec:word-lower "a"))
             (tptp-rec:role "axiom")
             (tptp-rec:atom (tptp-rec:word-lower "p") (tptp-rec:terms-nil))
             (tptp-rec:annotation-none)
             (tptp-rec:span 0 15))
           (tptp-rec:inputs-nil))
         (tptp-rec:span 0 15)))
     (LCons (RecordCase "fof('X',axiom,p(X,'X'))."
       (tptp-rec:file
         (tptp-rec:inputs-cons
           (tptp-rec:fof
             (tptp-rec:name-word (tptp-rec:word-quoted "'X'"))
             (tptp-rec:role "axiom")
             (tptp-rec:atom (tptp-rec:word-lower "p")
               (tptp-rec:terms-cons (tptp-rec:var (tptp-rec:word-upper "X"))
                 (tptp-rec:terms-cons
                   (tptp-rec:fun (tptp-rec:word-quoted "'X'") (tptp-rec:terms-nil))
                   (tptp-rec:terms-nil))))
             (tptp-rec:annotation-none)
             (tptp-rec:span 0 24))
           (tptp-rec:inputs-nil))
         (tptp-rec:span 0 24)))
     (LCons (RecordCase "cnf(c,axiom,(~p(X) | q(X)))."
       (tptp-rec:file
         (tptp-rec:inputs-cons
           (tptp-rec:cnf
             (tptp-rec:name-word (tptp-rec:word-lower "c"))
             (tptp-rec:role "axiom")
             (tptp-rec:or
               (tptp-rec:not
                 (tptp-rec:atom (tptp-rec:word-lower "p")
                   (tptp-rec:terms-cons (tptp-rec:var (tptp-rec:word-upper "X"))
                     (tptp-rec:terms-nil))))
               (tptp-rec:atom (tptp-rec:word-lower "q")
                 (tptp-rec:terms-cons (tptp-rec:var (tptp-rec:word-upper "X"))
                   (tptp-rec:terms-nil))))
             (tptp-rec:annotation-none)
             (tptp-rec:span 0 28))
           (tptp-rec:inputs-nil))
         (tptp-rec:span 0 28)))
       LNil))))))
(= (record-test:duplicates $program $context $parser $cases)
   (let $names
        (record-test:names
          (record-test:observe $program $context $parser "fof(a,axiom,p).fof(a,axiom,p)."))
     (case (== $names (LCons "a" (LCons "a" LNil)))
       ((True (TptpOfficialSyntaxRecordsV1Summary $cases quoted-variable-distinct cnf-or-not duplicates-preserved))
        (False (TptpRecordDuplicateMismatch $names))))))
(= (record-test:corpus $program $context (EBNF:Parser $handle $receipt))
   (case (langdef:parse-authored-file $handle (system:arg 1))
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
       (let $file
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-rec-v1:node
                (record-test:one-root
                  (langdef:run-deterministic-equations $program
                    (LangDef:DeterministicCall tptp-read-v1:roots $context
                      (bnf:project-ebnf-trees $receipt $trees))))))
         (let $names (record-test:names $file)
           (case (== $names
             (LCons "agatha" (LCons "butler" (LCons "charles"
             (LCons "poorer_killer" (LCons "different_hates"
             (LCons "no_one_hates_everyone" (LCons "agatha_hates_agatha"
             (LCons "agatha_hates_charles" (LCons "killer_hates_victim"
             (LCons "same_hates" (LCons "butler_hates_poor"
             (LCons "prove_neither_charles_nor_butler_did_it" LNil)))))))))))))
             ((True corpus-cnf-names-preserved)
              (False (TptpRecordCorpusNamesMismatch $names)))))))
      ($fault (TptpRecordCorpusParseFailure $fault)))))
(= (record-test:use $program $context $parser)
   (case $parser
     (((EBNF:Parser $handle $receipt)
       (let $base (record-test:checks $program $context $parser)
         (let $rest (record-test:duplicates $program $context $parser $base)
           (let $answers (collapse
             (TptpRecordChecks $rest
               (record-test:corpus $program $context (EBNF:Parser $handle $receipt))))
             (let $_ (bnf:close-ebnf-parser $parser)
               (superpose $answers))))))
      ($fault (TptpRecordPreparationFailure $fault)))))
(= (record-test:prepare $program $context (bnf-v1:grammar-input $document $authority))
   (case (bnf:validate-ebnf-declarations $document $authority)
     (((BNFDeclarationValidationAcceptedV1 (BNFStartSomeV1 $start $_span) $_definitions $_lexicals)
       (let $selected
            (bnf-internal:ebnf-transform
              (LangDef:EquationSources
                "langdef/bnf/ebnf_declaration_view_v1.metta"
                "langdef/bnf/ebnf_reachable_view_v1.metta")
              (LangDef:DeterministicCall ebnf-reach-v1:select $document $start))
         (record-test:use $program $context (bnf:parser-from-ebnf-document $selected $authority))))
      ($fault (TptpRecordDeclarationFailure $fault)))))
(= (record-test:run $program $source $document)
   (case (== $source "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6")
     ((True
       (let $lexical
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-lex-v1:lower $document $source
                (bnf-v1:lexical-except (bnf-v1:scalars-cons 10 (bnf-v1:scalars-nil)))))
         (let $context (langdef:run-deterministic-equations $program
                        (LangDef:DeterministicCall tptp-read-v1:prepare $lexical))
           (record-test:prepare $program $context
             (langdef:run-deterministic-equations $program
               (LangDef:DeterministicCall tptp-syn-v1:compose $lexical))))))
      (False TptpRecordSourceDigestMismatch))))
!(let ($source $document)
       (record-test:document (tptp:extended-bnf-file->ast (system:arg 0)))
   (let $program
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_lowering_v1.metta"
            "langdef/tptp/official_lexical_to_ebnf_v1.metta"
            "langdef/tptp/official_syntax_to_ebnf_v1.metta"
            "langdef/tptp/official_syntax_observation_v1.metta"
            "langdef/tptp/official_syntax_records_v1.metta"))
     (case $program
       (((NativeHandle $_kind $_id)
         (let $answers (collapse (record-test:run $program $source $document))
           (let $_ (langdef:close-deterministic-equations $program)
             (superpose $answers))))
        ($fault (TptpRecordProgramFailure $fault))))))
