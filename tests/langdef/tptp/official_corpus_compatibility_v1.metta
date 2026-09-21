!(import! &self ../../../lib/lib_bnf.metta)

(= (compat-test:span $start $end) (bnf-v1:source-span $start $end))
(= (compat-test:literal-alternative)
   (bnf-v1:alternative
     (bnf-v1:elements-cons
       (bnf-v1:literal (bnf:text "x") (compat-test:span 10 11))
       (bnf-v1:elements-nil))
     (compat-test:span 10 11)))
(= (compat-test:target-rule)
   (bnf-v1:rule (bnf:text "tff_unitary_term")
     (bnf-v1:expression
       (bnf-v1:alternatives-cons
         (compat-test:literal-alternative)
         (bnf-v1:alternatives-nil))
       (compat-test:span 8 12))
     (compat-test:span 8 12)))
(= (compat-test:extended-target-rule)
   (bnf-v1:rule (bnf:text "tff_unitary_term")
     (bnf-v1:expression
       (bnf-v1:alternatives-cons
         (compat-test:literal-alternative)
         (bnf-v1:alternatives-cons
           (bnf-v1:alternative
             (bnf-v1:elements-cons
               (bnf-v1:reference (bnf:text "tff_prefix_unary")
                 (compat-test:span 8 12))
               (bnf-v1:elements-nil))
             (compat-test:span 8 12))
           (bnf-v1:alternatives-nil)))
       (compat-test:span 8 12))
     (compat-test:span 8 12)))
(= (compat-test:document)
   (bnf-v1:grammar-input
     (bnf-v1:document
       (bnf-v1:entries-cons
         (bnf-v1:comment (bnf:text "kept") (compat-test:span 0 4))
         (bnf-v1:entries-cons
           (compat-test:target-rule)
           (bnf-v1:entries-cons
             (bnf-v1:blank (compat-test:span 12 13))
             (bnf-v1:entries-nil))))
       (compat-test:span 0 13))
     CompatAuthority))
(= (compat-test:expected)
   (bnf-v1:grammar-input
     (bnf-v1:document
       (bnf-v1:entries-cons
         (bnf-v1:comment (bnf:text "kept") (compat-test:span 0 4))
         (bnf-v1:entries-cons
           (compat-test:extended-target-rule)
           (bnf-v1:entries-cons
             (bnf-v1:blank (compat-test:span 12 13))
             (bnf-v1:entries-nil))))
       (compat-test:span 0 13))
     CompatAuthority))
(= (compat-test:missing)
   (bnf-v1:grammar-input
     (bnf-v1:document
       (bnf-v1:entries-cons
         (bnf-v1:rule (bnf:text "other")
           (bnf-v1:expression (bnf-v1:alternatives-nil)
             (compat-test:span 0 1))
           (compat-test:span 0 1))
         (bnf-v1:entries-nil))
       (compat-test:span 0 1))
     CompatAuthority))
(= (compat-test:duplicate)
   (bnf-v1:grammar-input
     (bnf-v1:document
       (bnf-v1:entries-cons (compat-test:target-rule)
         (bnf-v1:entries-cons (compat-test:target-rule)
           (bnf-v1:entries-nil)))
       (compat-test:span 0 20))
     CompatAuthority))
(= (compat-test:infer $program $target $input)
   (langdef:run-deterministic-equations $program
     (LangDef:DeterministicCall
       tptp-corpus-ntt-v1:infer $target $input)))
(= (compat-test:consume $program $kind $judgment)
   (langdef:run-deterministic-equations $program
     (LangDef:DeterministicCall
       tptp-corpus-v1:consume $kind $judgment)))
(= (compat-test:target) (bnf:text "tff_unitary_term"))
(= (compat-test:judgment $program $input)
   (compat-test:infer $program (compat-test:target) $input))
(= (compat-test:stale-judgment)
   (TPTP:CorpusCompatibilityTargetNativeTypeV1
     (compat-test:document) (compat-test:target)
     (bnf-v1:entries-nil) (compat-test:target-rule)
     (bnf-v1:entries-nil)))

!(let $program
      (langdef:load-deterministic-equations
        (LangDef:EquationSources
          "langdef/bnf/plain_bnf_denotation_v1.metta"
          "langdef/tptp/official_corpus_compatibility_native_types_v1.metta"
          "langdef/tptp/official_corpus_compatibility_v1.metta"))
   (let $answers
        (collapse
          (TptpOfficialCorpusCompatibilityV1Summary
            (== (compat-test:consume $program
                  tptp-corpus-v1:tff-prefix-term
                  (compat-test:judgment $program (compat-test:document)))
                (compat-test:expected))
            (== (compat-test:judgment $program (compat-test:missing))
                (TPTP:CorpusCompatibilityTargetMissingV1
                  (compat-test:target)))
            (== (compat-test:judgment $program (compat-test:duplicate))
                (TPTP:CorpusCompatibilityTargetDuplicateV1
                  (compat-test:target)))
            (== (compat-test:consume $program
                  tptp-corpus-v1:tff-prefix-term
                  (compat-test:stale-judgment))
                (TPTP:CorpusCompatibilityJudgmentMismatchV1
                  (compat-test:target)))))
     (let $_ (langdef:close-deterministic-equations $program)
       (superpose $answers))))
