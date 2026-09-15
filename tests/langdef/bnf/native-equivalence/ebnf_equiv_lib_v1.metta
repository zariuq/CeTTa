; Shared definitions for the EBNF projection native-versus-authored gate.
; tests/support/check_ebnf_projection_native_equivalence_v1.sh substitutes
; @LIB_BNF@ and @PROJECTION_SOURCE@, then runs each corpus case in its own
; process so transient oracle memory cannot accumulate across cases.

!(import! &self "@LIB_BNF@")

; One total rule: PeTTa rule heads with overlapping matches fork
; nondeterministically, so a separate base rule would both loop below zero
; and return an alternative wrong list.
(= (equiv:scalars-upto $n)
   (if (== $n 0)
       (bnf-v1:scalars-cons 0 (bnf-v1:scalars-nil))
       (bnf-v1:scalars-cons $n (equiv:scalars-upto (- $n 1)))))

(= (equiv:scalars-reverse (bnf-v1:scalars-nil) $acc) $acc)
(= (equiv:scalars-reverse (bnf-v1:scalars-cons $head $tail) $acc)
   (equiv:scalars-reverse $tail (bnf-v1:scalars-cons $head $acc)))

(= (equiv:json-authority)
   (bnf:grammar-authority-from-string "json"
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration
           (bnf:text "strchar") "Class_strchar"
           (bnf-v1:lexical-except
             (equiv:scalars-reverse (equiv:scalars-upto 31)
               (bnf-v1:scalars-cons 34 (bnf-v1:scalars-cons 92 (bnf-v1:scalars-nil)))))
           "Token_strchar" (bnf-v1:lexical-origin "benchmark" 0))
         (bnf-v1:lexical-declarations-nil)))))

(= (equiv:csv-authority)
   (bnf:grammar-authority-from-string "file"
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration
           (bnf:text "qchar") "Class_qchar"
           (bnf-v1:lexical-except (bnf-v1:scalars-cons 34 (bnf-v1:scalars-nil)))
           "Token_qchar" (bnf-v1:lexical-origin "benchmark" 0))
         (bnf-v1:lexical-declarations-cons
           (bnf-v1:lexical-declaration
             (bnf:text "uchar") "Class_uchar"
             (bnf-v1:lexical-except
               (bnf-v1:scalars-cons 10 (bnf-v1:scalars-cons 13
                 (bnf-v1:scalars-cons 34 (bnf-v1:scalars-cons 44 (bnf-v1:scalars-nil))))))
             "Token_uchar" (bnf-v1:lexical-origin "benchmark" 0))
           (bnf-v1:lexical-declarations-nil))))))

; The authored oracle call, source-substitutable so the negative-control
; variant can run a mutated oracle on test-local inputs.
(= (equiv:authored $receipt $trees $allowance)
   (case $receipt
     (((EBNF:LoweredGrammar $original $authority $plain $origins)
        (bnf-internal:ebnf-transform-allowance
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "@PROJECTION_SOURCE@")
          (LangDef:DeterministicCall ebnf-v1:project-derivations
            $plain $origins $authority $trees)
          $allowance))
      ($malformed (BNF:ReadFault ebnf-lowering-receipt $malformed)))))

(= (equiv:compare $grammar-path $authority $input-path $allowance $expected)
   (case (bnf:parser-from-ebnf-file-with-authority $grammar-path $authority)
     (((EBNF:SourceParser $handle $gb $gs $receipt)
        (case (langdef:parse-authored-file $handle $input-path)
          (((LangDef:AuthoredParseAccepted $l $b $s $trees)
             (let $native (bnf:project-ebnf-trees $receipt $trees)
               (let $authored (equiv:authored $receipt $trees $allowance)
                 (and (== $expected accepted)
                   (equiv:values-equal $native $authored)))))
           ((LangDef:AuthoredParseRejected $l $b $s $offset $wanted) (== $expected rejected))
           ($fault parse-fault))))
       ($fault preparation-fault))))

(= (equiv:literal-document $text)
   (bnf-v1:document
     (bnf-v1:entries-cons
       (bnf-v1:rule (bnf-v1:text-cons 115 (bnf-v1:text-nil))
         (bnf-v1:expression
           (bnf-v1:alternatives-cons
             (bnf-v1:alternative
               (bnf-v1:elements-cons
                 (bnf-v1:literal $text (bnf-v1:source-span 0 3))
                 (bnf-v1:elements-nil))
               (bnf-v1:source-span 0 3))
             (bnf-v1:alternatives-nil))
           (bnf-v1:source-span 0 4))
         (bnf-v1:source-span 0 12))
       (bnf-v1:entries-nil))
     (bnf-v1:source-span 0 12)))

(= (equiv:literal-receipt $text)
   (EBNF:LoweredGrammar
     (equiv:literal-document $text)
     (bnf:default-authority "s")
     (equiv:literal-document $text)
     (ebnf-v1:origins-nil)))

(= (equiv:text $scalar)
   (bnf-v1:text-cons $scalar (bnf-v1:text-nil)))

(= (equiv:lexical-document)
   (bnf-v1:document (bnf-v1:entries-nil) (bnf-v1:source-span 0 0)))

(= (equiv:lexical-start-text)
   (bnf-v1:text-cons 116 (bnf-v1:text-nil)))

(= (equiv:lexical-receipt)
   (EBNF:LoweredGrammar
     (equiv:lexical-document)
     (bnf-v1:grammar-authority
       (bnf-v1:start (equiv:lexical-start-text))
       (bnf-v1:lexical-environment
         (bnf-v1:lexical-declarations-cons
           (bnf-v1:lexical-declaration
             (equiv:lexical-start-text) "ClassT"
             (bnf-v1:lexical-except (bnf-v1:scalars-cons 34 (bnf-v1:scalars-nil)))
             "TokenLabelT" (bnf-v1:lexical-origin "boundary" 0))
           (bnf-v1:lexical-declarations-nil))))
     (equiv:lexical-document)
     (ebnf-v1:origins-nil)))

; Equal faults cannot stand in for successful projection of a valid input.
(= (equiv:values-equal $native $authored)
   (case $native
     (((Error $op $detail) False)
      ($value (case $authored
        (((Error $op $detail) False)
         ($other (== $value $other))))))))

(= (equiv:parity $expected $receipt $trees)
   (let $native (bnf:project-ebnf-trees $receipt $trees)
     (let $authored (bnf:project-ebnf-trees-authored $receipt $trees)
       (case $native
         (((Error $n-op $n-detail)
           (case $authored
             (((Error $a-op $a-detail) (== $expected fault))
              ($a-other False))))
          ($n-value
           (case $authored
             (((Error $a-op $a-detail) False)
              ($a-value (and (== $expected value) (== $n-value $a-value)))))))))))

(= (equiv:max-i64) 9223372036854775807)

(= (equiv:min-i64) -9223372036854775808)
