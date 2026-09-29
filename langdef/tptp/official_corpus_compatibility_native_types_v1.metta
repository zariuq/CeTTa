; Structural NativeType discovery for the explicit TPTP corpus-compatibility
; profile.  A successful judgment is a zipper over the exact source grammar:
; reversed entries before the unique target, the target rule itself, and the
; unchanged suffix.  The consumer checks that these pieces reconstruct the
; carried source before using the judgment.
(gslt-presentation-v1 TptpOfficialCorpusCompatibilityNativeTypesV1
  (signature
    (operator metta-equation 2)
    (operator langdef:ground-equal 2)
    (operator tptp-corpus-ntt-v1:infer 2)
    (operator tptp-corpus-ntt-v1:scan-before 4)
    (operator tptp-corpus-ntt-v1:scan-before-entry 6)
    (operator tptp-corpus-ntt-v1:scan-after 6)
    (operator tptp-corpus-ntt-v1:scan-after-entry 7)
    (operator bnf-v1:grammar-input 2)
    (operator bnf-v1:document 2)
    (operator bnf-v1:entries-nil 0)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator TPTP:CorpusCompatibilityTargetNativeTypeV1 5)
    (operator TPTP:CorpusCompatibilityTargetMissingV1 1)
    (operator TPTP:CorpusCompatibilityTargetDuplicateV1 1))
  (equations)
  (rewrites
    (rule corpus-compatibility-native-type-start (head (metta-equation
      (tptp-corpus-ntt-v1:infer ?target
        (bnf-v1:grammar-input
          (bnf-v1:document ?entries ?document-span) ?authority))
      (tptp-corpus-ntt-v1:scan-before ?target
        (bnf-v1:grammar-input
          (bnf-v1:document ?entries ?document-span) ?authority)
        (bnf-v1:entries-nil) ?entries))) (body))

    (rule corpus-compatibility-native-type-missing (head (metta-equation
      (tptp-corpus-ntt-v1:scan-before
        ?target ?source ?prefix-reverse (bnf-v1:entries-nil))
      (TPTP:CorpusCompatibilityTargetMissingV1 ?target))) (body))
    (rule corpus-compatibility-native-type-before-rule (head (metta-equation
      (tptp-corpus-ntt-v1:scan-before ?target ?source ?prefix-reverse
        (bnf-v1:entries-cons
          (bnf-v1:rule ?name ?expression ?span) ?tail))
      (tptp-corpus-ntt-v1:scan-before-entry
        (langdef:ground-equal ?target ?name)
        ?target ?source ?prefix-reverse
        (bnf-v1:rule ?name ?expression ?span) ?tail))) (body))
    (rule corpus-compatibility-native-type-before-comment (head (metta-equation
      (tptp-corpus-ntt-v1:scan-before ?target ?source ?prefix-reverse
        (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) ?tail))
      (tptp-corpus-ntt-v1:scan-before ?target ?source
        (bnf-v1:entries-cons
          (bnf-v1:comment ?text ?span) ?prefix-reverse)
        ?tail))) (body))
    (rule corpus-compatibility-native-type-before-blank (head (metta-equation
      (tptp-corpus-ntt-v1:scan-before ?target ?source ?prefix-reverse
        (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail))
      (tptp-corpus-ntt-v1:scan-before ?target ?source
        (bnf-v1:entries-cons (bnf-v1:blank ?span) ?prefix-reverse)
        ?tail))) (body))

    (rule corpus-compatibility-native-type-before-other (head (metta-equation
      (tptp-corpus-ntt-v1:scan-before-entry LangDef:DifferentValue
        ?target ?source ?prefix-reverse ?rule ?tail)
      (tptp-corpus-ntt-v1:scan-before ?target ?source
        (bnf-v1:entries-cons ?rule ?prefix-reverse) ?tail))) (body))
    (rule corpus-compatibility-native-type-before-target (head (metta-equation
      (tptp-corpus-ntt-v1:scan-before-entry LangDef:SameValue
        ?target ?source ?prefix-reverse ?rule ?tail)
      (tptp-corpus-ntt-v1:scan-after
        ?target ?source ?prefix-reverse ?rule ?tail ?tail))) (body))

    (rule corpus-compatibility-native-type-unique (head (metta-equation
      (tptp-corpus-ntt-v1:scan-after
        ?target ?source ?prefix-reverse ?rule
        (bnf-v1:entries-nil) ?suffix)
      (TPTP:CorpusCompatibilityTargetNativeTypeV1
        ?source ?target ?prefix-reverse ?rule ?suffix))) (body))
    (rule corpus-compatibility-native-type-after-rule (head (metta-equation
      (tptp-corpus-ntt-v1:scan-after
        ?target ?source ?prefix-reverse ?target-rule
        (bnf-v1:entries-cons
          (bnf-v1:rule ?name ?expression ?span) ?tail)
        ?suffix)
      (tptp-corpus-ntt-v1:scan-after-entry
        (langdef:ground-equal ?target ?name)
        ?target ?source ?prefix-reverse ?target-rule ?tail ?suffix))) (body))
    (rule corpus-compatibility-native-type-after-comment (head (metta-equation
      (tptp-corpus-ntt-v1:scan-after
        ?target ?source ?prefix-reverse ?target-rule
        (bnf-v1:entries-cons (bnf-v1:comment ?text ?span) ?tail)
        ?suffix)
      (tptp-corpus-ntt-v1:scan-after
        ?target ?source ?prefix-reverse ?target-rule ?tail ?suffix))) (body))
    (rule corpus-compatibility-native-type-after-blank (head (metta-equation
      (tptp-corpus-ntt-v1:scan-after
        ?target ?source ?prefix-reverse ?target-rule
        (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail)
        ?suffix)
      (tptp-corpus-ntt-v1:scan-after
        ?target ?source ?prefix-reverse ?target-rule ?tail ?suffix))) (body))

    (rule corpus-compatibility-native-type-after-other (head (metta-equation
      (tptp-corpus-ntt-v1:scan-after-entry LangDef:DifferentValue
        ?target ?source ?prefix-reverse ?target-rule ?tail ?suffix)
      (tptp-corpus-ntt-v1:scan-after
        ?target ?source ?prefix-reverse ?target-rule ?tail ?suffix))) (body))
    (rule corpus-compatibility-native-type-duplicate (head (metta-equation
      (tptp-corpus-ntt-v1:scan-after-entry LangDef:SameValue
        ?target ?source ?prefix-reverse ?target-rule ?tail ?suffix)
      (TPTP:CorpusCompatibilityTargetDuplicateV1 ?target))) (body))))
