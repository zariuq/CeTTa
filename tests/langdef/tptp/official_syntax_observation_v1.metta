!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self system)

; Keep the test consumer separate from the authored reader transformation.
(= (read-test:document
     (TPTP:StageValue extended-bnf-ast (TPTP:ExtendedBnfAst $_binding $source $document)))
   ($source $document))
(= (read-test:observe $program $context (EBNF:Parser $handle $receipt) $text)
   (case (langdef:parse-authored-text $handle $text)
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
       (langdef:run-deterministic-equations $program
         (LangDef:DeterministicCall tptp-read-v1:roots $context
           (bnf:project-ebnf-trees $receipt $trees))))
      ($fault (TptpObservationParseFailure $fault)))))
(= (read-test:tokens LNil $tail) $tail)
(= (read-test:tokens (LCons $head $rest) $tail)
   (read-test:token-node $head (read-test:tokens $rest $tail)))
(= (read-test:token-node $node $tail)
   (case $node
     (((tptp-cst:spanned-node $_production $_start $_stop $children)
       (read-test:tokens $children $tail))
      ((tptp-cst:token $kind $start $stop $text)
       (LCons (ObservedToken $kind $start $stop $text) $tail))
      ((tptp-cst:literal $_start $_stop $_text) $tail)
      ($fault (TptpObservationUnexpectedNode $fault)))))
(= (read-test:spellings $tokens)
   (case $tokens
     ((LNil LNil)
      ((LCons (ObservedToken $kind $_start $_stop $text) $tail)
       (LCons (Token $kind $text) (read-test:spellings $tail)))
      ($fault (TptpObservationUnexpectedTokens $fault)))))
(= (read-test:one-root $result)
   (case $result
     (((LCons (LCons $root LNil) LNil) $root)
      ($fault (TptpObservationRootFailure $fault)))))
; A MeTTa consumer of the structured syntax, not another parser. Select
; occurrences by their official source category, preserving traversal order.
(= (read-test:select-list $name LNil $tail) $tail)
(= (read-test:select-list $name (LCons $head $rest) $tail)
   (read-test:select-node $name $head (read-test:select-list $name $rest $tail)))
(= (read-test:select-node $name $node $tail)
   (case $node
     (((tptp-cst:spanned-node (tptp-cst:production $found $_alternative) $_start $_stop $children)
       (let $rest (read-test:select-list $name $children $tail)
         (case (== $name $found)
           ((True (LCons (read-test:spellings (read-test:token-node $node LNil)) $rest))
            (False $rest)))))
      ((tptp-cst:token $_kind $_start $_stop $_text) $tail)
      ((tptp-cst:literal $_start $_stop $_text) $tail)
      ($fault (TptpObservationSelectFailure $fault)))))
(= (read-test:cases $program $context $parser LNil) 0)
(= (read-test:cases $program $context $parser (LCons (ReadCase $text $expected) $rest))
   (let $root (read-test:one-root (read-test:observe $program $context $parser $text))
     (let $actual (read-test:spellings (read-test:token-node $root LNil))
       (case (== $actual $expected)
         ((True (+ 1 (read-test:cases $program $context $parser $rest)))
          (False (TptpObservationMismatch $text $expected $actual)))))))
(= (read-test:checks $program $context $parser)
   (let $cases
     (read-test:cases $program $context $parser
       (LCons (ReadCase "" LNil)
       (LCons (ReadCase "fof(a,axiom,p)."
         (LCons (Token "lower_word" "a") (LCons (Token "lower_word" "axiom")
           (LCons (Token "lower_word" "p") LNil))))
       (LCons (ReadCase "% p(X) is a comment\nfof(a,axiom,p). /* ignored */"
         (LCons (Token "lower_word" "a") (LCons (Token "lower_word" "axiom")
           (LCons (Token "lower_word" "p") LNil))))
       (LCons (ReadCase "fof('X',axiom,p(X,'X'))."
         (LCons (Token "single_quoted" "'X'") (LCons (Token "lower_word" "axiom")
           (LCons (Token "lower_word" "p") (LCons (Token "upper_word" "X")
             (LCons (Token "single_quoted" "'X'") LNil))))))
       (LCons (ReadCase "tff(t,type,f:$i>$i)."
         (LCons (Token "lower_word" "t") (LCons (Token "lower_word" "type")
           (LCons (Token "lower_word" "f") (LCons (Token "dollar_word" "$i")
             (LCons (Token "arrow" ">") (LCons (Token "dollar_word" "$i") LNil)))))))
       (LCons (ReadCase "thf(h,axiom,(! [X:$i] : (p @ X)))."
         (LCons (Token "lower_word" "h") (LCons (Token "lower_word" "axiom")
           (LCons (Token "upper_word" "X") (LCons (Token "dollar_word" "$i")
             (LCons (Token "lower_word" "p") (LCons (Token "upper_word" "X") LNil)))))))
       (LCons (ReadCase "tcf(c,axiom,! [X:$i] : p(X))."
         (LCons (Token "lower_word" "c") (LCons (Token "lower_word" "axiom")
           (LCons (Token "upper_word" "X") (LCons (Token "dollar_word" "$i")
             (LCons (Token "lower_word" "p") (LCons (Token "upper_word" "X") LNil)))))))
       (LCons (ReadCase "tpi(i,axiom,p)."
         (LCons (Token "lower_word" "i") (LCons (Token "lower_word" "axiom")
           (LCons (Token "lower_word" "p") LNil))))
       (LCons (ReadCase "cnf(c,axiom,(~p(X) | q(X)))."
         (LCons (Token "lower_word" "c") (LCons (Token "lower_word" "axiom")
           (LCons (Token "lower_word" "p") (LCons (Token "upper_word" "X")
             (LCons (Token "vline" "|") (LCons (Token "lower_word" "q")
               (LCons (Token "upper_word" "X") LNil))))))))
       (LCons (ReadCase "include('a.ax',[a,a])."
         (LCons (Token "single_quoted" "'a.ax'") (LCons (Token "lower_word" "a")
           (LCons (Token "lower_word" "a") LNil))))
       (LCons (ReadCase "fof(a,axiom,p).fof(a,axiom,p)."
         (LCons (Token "lower_word" "a") (LCons (Token "lower_word" "axiom")
           (LCons (Token "lower_word" "p") (LCons (Token "lower_word" "a")
             (LCons (Token "lower_word" "axiom") (LCons (Token "lower_word" "p") LNil)))))))
       (LCons (ReadCase "fof(f,plain,p,inference(resolution,[status(thm)],[a,b]))."
         (LCons (Token "lower_word" "f") (LCons (Token "lower_word" "plain")
           (LCons (Token "lower_word" "p") (LCons (Token "lower_word" "resolution")
             (LCons (Token "lower_word" "status") (LCons (Token "lower_word" "thm")
               (LCons (Token "lower_word" "a") (LCons (Token "lower_word" "b") LNil)))))))))
       (LCons (ReadCase "fof('a\\'b',axiom,p)."
         (LCons (Token "single_quoted" "'a\\'b'") (LCons (Token "lower_word" "axiom")
           (LCons (Token "lower_word" "p") LNil))))
         LNil))))))))))))))
     (let $root (read-test:one-root (read-test:observe $program $context $parser "fof(a ,axiom,p)."))
       (let $spans (read-test:token-node $root LNil)
         (case (== $spans
           (LCons (ObservedToken "lower_word" 4 5 "a")
             (LCons (ObservedToken "lower_word" 7 12 "axiom")
               (LCons (ObservedToken "lower_word" 13 14 "p") LNil))))
           ((True (read-test:unicode-spans $program $context $parser $cases))
            (False (TptpObservationSpanMismatch $spans))))))))
; The official line-comment class admits Unicode; sq_char does not. The prefix
; makes scalar coordinates differ from UTF-8 byte offsets for every token.
(= (read-test:unicode-spans $program $context $parser $cases)
   (let $root (read-test:one-root (read-test:observe $program $context $parser "%λ\nfof(a,axiom,p)."))
     (let $spans (read-test:token-node $root LNil)
       (case (== $spans
         (LCons (ObservedToken "lower_word" 7 8 "a")
           (LCons (ObservedToken "lower_word" 9 14 "axiom")
             (LCons (ObservedToken "lower_word" 15 16 "p") LNil))))
         ((True (read-test:refusals $program $context $parser $cases))
          (False (TptpObservationUnicodeSpanMismatch $spans)))))))
(= (read-test:refusal $result)
   (case $result
     (((Error $_call $_detail) refused)
      ($other (TptpObservationExpectedRefusal $other)))))
(= (read-test:refusals $program $context (EBNF:Parser $handle $receipt) $cases)
   (let $lexical
     (case (langdef:parse-authored-text $handle "fof('λ',axiom,p).")
       (((LangDef:AuthoredParseRejected $_language $_binding $_source 5 $_expected) refused)
        ($other (TptpObservationExpectedLexicalRefusal $other))))
     (let $unknown (read-test:refusal
       (langdef:run-deterministic-equations $program
         (LangDef:DeterministicCall tptp-read-v1:kind $context "not-an-official-rule")))
       (let $mismatch (read-test:refusal
         (langdef:run-deterministic-equations $program
           (LangDef:DeterministicCall tptp-read-v1:checked-token
             LangDef:DifferentValue "lower_word" 0 1 unused-tree LNil)))
         (case (== ($lexical $unknown $mismatch) (refused refused refused))
           ((True (read-test:binding-structure $program $context (EBNF:Parser $handle $receipt) $cases))
            (False (TptpObservationRefusalMismatch $lexical $unknown $mismatch))))))))
(= (read-test:binding-structure $program $context (EBNF:Parser $handle $receipt) $cases)
   (let $root (read-test:one-root
     (read-test:observe $program $context (EBNF:Parser $handle $receipt)
       "thf(h,axiom,(! [X:$i] : (p @ X)))."))
     (let $binder (read-test:select-node "thf_variable_list" $root LNil)
       (let $application (read-test:select-node "thf_apply_formula" $root LNil)
         (case (== ($binder $application)
           ((LCons (LCons (Token "upper_word" "X") (LCons (Token "dollar_word" "$i") LNil)) LNil)
            (LCons (LCons (Token "lower_word" "p") (LCons (Token "upper_word" "X") LNil)) LNil)))
           ((True (read-test:agreement $program $context $handle $receipt $cases))
            (False (TptpObservationBindingStructureMismatch $binder $application))))))))
(= (read-test:agreement $program $context $handle $receipt $cases)
   (case (langdef:parse-authored-text $handle "fof(a,axiom,p).")
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
       (let $native (bnf:project-ebnf-trees $receipt $trees)
         (let $authored (bnf:project-ebnf-trees-authored $receipt $trees)
           (case (== $native $authored)
             ((True (read-test:multiplicity $program $context $native $cases))
              (False (TptpObservationEbnfAgreementMismatch $native $authored)))))))
      ($fault (TptpObservationAgreementParseFailure $fault)))))
(= (read-test:multiplicity $program $context $derivations $cases)
   (case $derivations
     (((EBNF:DerivationsCons $root (EBNF:DerivationsNil))
       (let $single (langdef:run-deterministic-equations $program
         (LangDef:DeterministicCall tptp-read-v1:roots $context $derivations))
         (let $double (langdef:run-deterministic-equations $program
           (LangDef:DeterministicCall tptp-read-v1:roots $context
             (EBNF:DerivationsCons $root (EBNF:DerivationsCons $root (EBNF:DerivationsNil)))))
           (case $single
             (((LCons $projected LNil)
               (case (== $double (LCons $projected (LCons $projected LNil)))
                 ((True (TptpOfficialSyntaxObservationV1Summary $cases scalar-spans-preserved 3 binding-structure-preserved authored-agreement multiplicity-preserved))
                  (False (TptpObservationMultiplicityMismatch $single $double)))))
              ($fault (TptpObservationSingleRootMismatch $fault)))))))
      ($fault (TptpObservationUnexpectedDerivations $fault)))))
(= (read-test:names $selected)
   (case $selected
     ((LNil LNil)
      ((LCons (LCons (Token "lower_word" $name) LNil) $tail)
       (LCons $name (read-test:names $tail)))
      ($fault (TptpObservationNameShapeMismatch $fault)))))
(= (read-test:corpus $program $context (EBNF:Parser $handle $receipt))
   (case (langdef:parse-authored-file $handle (system:arg 1))
     (((LangDef:AuthoredParseAccepted $_language $_binding $_source $trees)
       (let $root (read-test:one-root
         (langdef:run-deterministic-equations $program
           (LangDef:DeterministicCall tptp-read-v1:roots $context
             (bnf:project-ebnf-trees $receipt $trees))))
         (let $names (read-test:names (read-test:select-node "name" $root LNil))
           (case (== $names
             (LCons "agatha" (LCons "butler" (LCons "charles"
             (LCons "poorer_killer" (LCons "different_hates"
             (LCons "no_one_hates_everyone" (LCons "agatha_hates_agatha"
             (LCons "agatha_hates_charles" (LCons "killer_hates_victim"
             (LCons "same_hates" (LCons "butler_hates_poor"
             (LCons "prove_neither_charles_nor_butler_did_it" LNil)))))))))))))
             ((True corpus-names-preserved)
              (False (TptpObservationCorpusNamesMismatch $names)))))))
      ($fault (TptpObservationCorpusParseFailure $fault)))))
(= (read-test:use $program $context $parser)
   (case $parser
     (((EBNF:Parser $_handle $_receipt)
       (let $answers (collapse
         (TptpObservationChecks
           (read-test:checks $program $context $parser)
           (read-test:corpus $program $context $parser)))
         (let $_ (bnf:close-ebnf-parser $parser)
           (superpose $answers))))
      ($fault (TptpObservationPreparationFailure $fault)))))
(= (read-test:prepare $program $context (bnf-v1:grammar-input $document $authority))
   (case (bnf:validate-ebnf-declarations $document $authority)
     (((BNFDeclarationValidationAcceptedV1 (BNFStartSomeV1 $start $_span) $_definitions $_lexicals)
       (let $selected
            (bnf-internal:ebnf-transform
              (LangDef:EquationSources
                "langdef/bnf/ebnf_declaration_view_v1.metta"
                "langdef/bnf/ebnf_reachable_view_v1.metta")
              (LangDef:DeterministicCall ebnf-reach-v1:select $document $start))
         (read-test:use $program $context (bnf:parser-from-ebnf-document $selected $authority))))
      ($fault (TptpObservationDeclarationFailure $fault)))))
(= (read-test:run $program $source $document)
   (case (== $source "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6")
     ((True
       (let $lexical
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-lex-v1:lower $document $source
                (bnf-v1:lexical-except (bnf-v1:scalars-cons 10 (bnf-v1:scalars-nil)))))
         (let $context (langdef:run-deterministic-equations $program
                        (LangDef:DeterministicCall tptp-read-v1:prepare $lexical))
           (read-test:prepare $program $context
             (langdef:run-deterministic-equations $program
               (LangDef:DeterministicCall tptp-syn-v1:compose $lexical))))))
      (False TptpObservationSourceDigestMismatch))))
!(let ($source $document)
       (read-test:document (tptp:extended-bnf-file->ast (system:arg 0)))
   (let $program
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_lowering_v1.metta"
            "langdef/tptp/official_lexical_to_ebnf_v1.metta"
            "langdef/tptp/official_syntax_to_ebnf_v1.metta"
            "langdef/tptp/official_syntax_observation_v1.metta"))
     (case $program
       (((NativeHandle $_kind $_id)
         (let $answers (collapse (read-test:run $program $source $document))
           (let $_ (langdef:close-deterministic-equations $program)
             (superpose $answers))))
        ($fault (TptpObservationProgramFailure $fault))))))
