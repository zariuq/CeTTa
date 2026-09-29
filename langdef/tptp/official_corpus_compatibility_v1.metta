; Explicit compatibility profile for spellings present in the pinned
; distribution but excluded by its pinned SyntaxBNF.  The strict
; official grammar remains unchanged.  Each extension consumes a structural
; NativeType judgment produced from the current grammar; the judgment is
; rejected unless its zipper reconstructs the exact carried source.
; The profile's reader snapshot is constructed against the strict snapshot
; and marks the productions these extensions add as avoided: a text is read
; by a derivation using the fewest of them, so a text the strict grammar
; reads keeps its strict reading, and a tie among those derivations remains
; ambiguous.
(gslt-presentation-v1 TptpOfficialCorpusCompatibilityV1
  (signature
    (operator metta-equation 2)
    (operator langdef:ground-equal 2)
    (operator bnf-v1:string->text 1)
    (operator tptp-corpus-ntt-v1:infer 2)
    (operator tptp-corpus-v1:extend-formula-terms 1)
    (operator tptp-corpus-v1:profile 2)
    (operator tptp-corpus-v1:plan-nil 0)
    (operator tptp-corpus-v1:plan-cons 3)
    (operator tptp-corpus-v1:after-discovery 3)
    (operator tptp-corpus-v1:after-extension 2)
    (operator tptp-corpus-v1:consume 2)
    (operator tptp-corpus-v1:consume-target 7)
    (operator tptp-corpus-v1:consume-source 7)
    (operator tptp-corpus-v1:restore-prefix 2)
    (operator tptp-corpus-v1:extend-rule 2)
    (operator tptp-corpus-v1:extend-expression 3)
    (operator tptp-corpus-v1:append-alternatives 2)
    (operator tptp-corpus-v1:new-alternatives 2)
    (operator tptp-corpus-v1:tff-prefix-term 0)
    (operator tptp-corpus-v1:introduced-two-arguments 0)
    (operator tptp-corpus-v1:nhf-bare-parameter 0)
    (operator tptp-corpus-v1:parenthesized-cnf-infix-in-disjunction 0)
    (operator bnf-v1:grammar-input 2)
    (operator bnf-v1:document 2)
    (operator bnf-v1:entries-nil 0)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:alternatives-nil 0)
    (operator bnf-v1:alternatives-cons 2)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:elements-nil 0)
    (operator bnf-v1:elements-cons 2)
    (operator bnf-v1:reference 2)
    (operator bnf-v1:literal 2)
    (operator TPTP:CorpusCompatibilityTargetNativeTypeV1 5)
    (operator TPTP:CorpusCompatibilityTargetMissingV1 1)
    (operator TPTP:CorpusCompatibilityTargetDuplicateV1 1)
    (operator TPTP:CorpusCompatibilityJudgmentMismatchV1 1))
  (equations)
  (rewrites
    (rule corpus-compatibility-start (head (metta-equation
      (tptp-corpus-v1:extend-formula-terms ?input)
      (tptp-corpus-v1:profile ?input
        (tptp-corpus-v1:plan-cons
          (bnf-v1:string->text "tff_unitary_term")
          tptp-corpus-v1:tff-prefix-term
        (tptp-corpus-v1:plan-cons
          (bnf-v1:string->text "internal_source")
          tptp-corpus-v1:introduced-two-arguments
        (tptp-corpus-v1:plan-cons
          (bnf-v1:string->text "nhf_parameter")
          tptp-corpus-v1:nhf-bare-parameter
        (tptp-corpus-v1:plan-cons
          (bnf-v1:string->text "cnf_disjunction")
          tptp-corpus-v1:parenthesized-cnf-infix-in-disjunction
          (tptp-corpus-v1:plan-nil)))))))) (body))

    (rule corpus-compatibility-profile-done (head (metta-equation
      (tptp-corpus-v1:profile ?input (tptp-corpus-v1:plan-nil))
      ?input)) (body))
    (rule corpus-compatibility-profile-step (head (metta-equation
      (tptp-corpus-v1:profile ?input
        (tptp-corpus-v1:plan-cons ?target ?kind ?tail))
      (tptp-corpus-v1:after-discovery
        (tptp-corpus-ntt-v1:infer ?target ?input) ?kind ?tail))) (body))

    (rule corpus-compatibility-after-discovery (head (metta-equation
      (tptp-corpus-v1:after-discovery
        (TPTP:CorpusCompatibilityTargetNativeTypeV1
          ?source ?target ?prefix-reverse ?rule ?suffix)
        ?kind ?tail)
      (tptp-corpus-v1:after-extension
        (tptp-corpus-v1:consume ?kind
          (TPTP:CorpusCompatibilityTargetNativeTypeV1
            ?source ?target ?prefix-reverse ?rule ?suffix))
        ?tail))) (body))
    (rule corpus-compatibility-after-discovery-missing (head (metta-equation
      (tptp-corpus-v1:after-discovery
        (TPTP:CorpusCompatibilityTargetMissingV1 ?target) ?kind ?tail)
      (TPTP:CorpusCompatibilityTargetMissingV1 ?target))) (body))
    (rule corpus-compatibility-after-discovery-duplicate (head (metta-equation
      (tptp-corpus-v1:after-discovery
        (TPTP:CorpusCompatibilityTargetDuplicateV1 ?target) ?kind ?tail)
      (TPTP:CorpusCompatibilityTargetDuplicateV1 ?target))) (body))

    (rule corpus-compatibility-after-extension (head (metta-equation
      (tptp-corpus-v1:after-extension
        (bnf-v1:grammar-input ?document ?authority) ?tail)
      (tptp-corpus-v1:profile
        (bnf-v1:grammar-input ?document ?authority) ?tail))) (body))
    (rule corpus-compatibility-after-extension-mismatch (head (metta-equation
      (tptp-corpus-v1:after-extension
        (TPTP:CorpusCompatibilityJudgmentMismatchV1 ?target) ?tail)
      (TPTP:CorpusCompatibilityJudgmentMismatchV1 ?target))) (body))

    (rule corpus-compatibility-consume (head (metta-equation
      (tptp-corpus-v1:consume ?kind
        (TPTP:CorpusCompatibilityTargetNativeTypeV1
          (bnf-v1:grammar-input
            (bnf-v1:document ?source-entries ?document-span) ?authority)
          ?target ?prefix-reverse
          (bnf-v1:rule ?name ?expression ?rule-span) ?suffix))
      (tptp-corpus-v1:consume-target
        (langdef:ground-equal ?target ?name)
        ?kind
        (bnf-v1:grammar-input
          (bnf-v1:document ?source-entries ?document-span) ?authority)
        ?target ?prefix-reverse
        (bnf-v1:rule ?name ?expression ?rule-span) ?suffix))) (body))
    (rule corpus-compatibility-consume-target-mismatch (head (metta-equation
      (tptp-corpus-v1:consume-target LangDef:DifferentValue
        ?kind ?source ?target ?prefix-reverse ?rule ?suffix)
      (TPTP:CorpusCompatibilityJudgmentMismatchV1 ?target))) (body))
    (rule corpus-compatibility-consume-target (head (metta-equation
      (tptp-corpus-v1:consume-target LangDef:SameValue
        ?kind
        (bnf-v1:grammar-input
          (bnf-v1:document ?source-entries ?document-span) ?authority)
        ?target ?prefix-reverse ?rule ?suffix)
      (tptp-corpus-v1:consume-source
        (langdef:ground-equal ?source-entries
          (tptp-corpus-v1:restore-prefix ?prefix-reverse
            (bnf-v1:entries-cons ?rule ?suffix)))
        ?kind
        (bnf-v1:grammar-input
          (bnf-v1:document ?source-entries ?document-span) ?authority)
        ?target ?prefix-reverse ?rule ?suffix))) (body))
    (rule corpus-compatibility-consume-source-mismatch (head (metta-equation
      (tptp-corpus-v1:consume-source LangDef:DifferentValue
        ?kind ?source ?target ?prefix-reverse ?rule ?suffix)
      (TPTP:CorpusCompatibilityJudgmentMismatchV1 ?target))) (body))
    (rule corpus-compatibility-consume-source (head (metta-equation
      (tptp-corpus-v1:consume-source LangDef:SameValue
        ?kind
        (bnf-v1:grammar-input
          (bnf-v1:document ?source-entries ?document-span) ?authority)
        ?target ?prefix-reverse ?rule ?suffix)
      (bnf-v1:grammar-input
        (bnf-v1:document
          (tptp-corpus-v1:restore-prefix ?prefix-reverse
            (bnf-v1:entries-cons
              (tptp-corpus-v1:extend-rule ?kind ?rule) ?suffix))
          ?document-span)
        ?authority))) (body))

    (rule corpus-compatibility-restore-empty (head (metta-equation
      (tptp-corpus-v1:restore-prefix (bnf-v1:entries-nil) ?tail)
      ?tail)) (body))
    (rule corpus-compatibility-restore-cons (head (metta-equation
      (tptp-corpus-v1:restore-prefix
        (bnf-v1:entries-cons ?head ?prefix-tail) ?tail)
      (tptp-corpus-v1:restore-prefix ?prefix-tail
        (bnf-v1:entries-cons ?head ?tail)))) (body))

    (rule corpus-compatibility-extend-rule (head (metta-equation
      (tptp-corpus-v1:extend-rule ?kind
        (bnf-v1:rule ?name ?expression ?rule-span))
      (bnf-v1:rule ?name
        (tptp-corpus-v1:extend-expression
          ?kind ?expression ?rule-span)
        ?rule-span))) (body))
    (rule corpus-compatibility-extend-expression (head (metta-equation
      (tptp-corpus-v1:extend-expression ?kind
        (bnf-v1:expression ?alternatives ?expression-span) ?rule-span)
      (bnf-v1:expression
        (tptp-corpus-v1:append-alternatives ?alternatives
          (tptp-corpus-v1:new-alternatives ?kind ?rule-span))
        ?expression-span))) (body))
    (rule corpus-compatibility-append-empty (head (metta-equation
      (tptp-corpus-v1:append-alternatives
        (bnf-v1:alternatives-nil) ?right)
      ?right)) (body))
    (rule corpus-compatibility-append-cons (head (metta-equation
      (tptp-corpus-v1:append-alternatives
        (bnf-v1:alternatives-cons ?head ?tail) ?right)
      (bnf-v1:alternatives-cons ?head
        (tptp-corpus-v1:append-alternatives ?tail ?right)))) (body))

    (rule corpus-compatibility-tff-prefix-term (head (metta-equation
      (tptp-corpus-v1:new-alternatives
        tptp-corpus-v1:tff-prefix-term ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "tff_prefix_unary") ?span)
            (bnf-v1:elements-nil))
          ?span)
        (bnf-v1:alternatives-nil)))) (body))
    (rule corpus-compatibility-introduced-two-arguments (head (metta-equation
      (tptp-corpus-v1:new-alternatives
        tptp-corpus-v1:introduced-two-arguments ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "introduced") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "intro_type") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ",") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "useful_info") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
            (bnf-v1:elements-nil)))))))))))
          ?span)
        (bnf-v1:alternatives-nil)))) (body))
    (rule corpus-compatibility-nhf-bare-parameter (head (metta-equation
      (tptp-corpus-v1:new-alternatives
        tptp-corpus-v1:nhf-bare-parameter ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "tff_unitary_term") ?span)
            (bnf-v1:elements-nil))
          ?span)
        (bnf-v1:alternatives-nil)))) (body))
    (rule corpus-compatibility-parenthesized-cnf-infix-in-disjunction
      (head (metta-equation
      (tptp-corpus-v1:new-alternatives
        tptp-corpus-v1:parenthesized-cnf-infix-in-disjunction ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_defined_infix_formula") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#token:vline") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "cnf_literal") ?span)
            (bnf-v1:elements-nil))))))))
          ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_infix_unary") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#token:vline") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "cnf_literal") ?span)
            (bnf-v1:elements-nil))))))))
          ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "cnf_disjunction") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#token:vline") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_defined_infix_formula") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
            (bnf-v1:elements-nil))))))))
          ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "cnf_disjunction") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#token:vline") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_infix_unary") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
            (bnf-v1:elements-nil))))))))
          ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_defined_infix_formula") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#token:vline") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_defined_infix_formula") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
            (bnf-v1:elements-nil))))))))))))
          ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_defined_infix_formula") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#token:vline") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_infix_unary") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
            (bnf-v1:elements-nil))))))))))))
          ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_infix_unary") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#token:vline") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_defined_infix_formula") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
            (bnf-v1:elements-nil))))))))))))
          ?span)
      (bnf-v1:alternatives-cons
        (bnf-v1:alternative
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_infix_unary") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#token:vline") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text "(") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference
              (bnf-v1:string->text "fof_infix_unary") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:literal (bnf-v1:string->text ")") ?span)
          (bnf-v1:elements-cons
            (bnf-v1:reference (bnf-v1:string->text "#layout") ?span)
            (bnf-v1:elements-nil))))))))))))
          ?span)
        (bnf-v1:alternatives-nil))))))))))) (body))))
