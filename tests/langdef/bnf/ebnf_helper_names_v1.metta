!(import! &self ../../../lib/lib_bnf.metta)

; Every edited optional has the same body and span. Only occurrence identity
; may distinguish its helper; the cases cross binary counter carry boundaries.
(= (fresh-test:span) (bnf-v1:source-span 0 0))
(= (fresh-test:literal $value)
   (bnf-v1:literal (bnf:text $value) (fresh-test:span)))
(= (fresh-test:optionals $count)
   (if (== $count 0) (bnf-v1:elements-nil)
     (bnf-v1:elements-cons
       (ebnf-v1:optional (fresh-test:literal "x") (fresh-test:span))
       (fresh-test:optionals (- $count 1)))))
(= (fresh-test:rule $name $elements)
   (bnf-v1:rule (bnf:text $name)
     (bnf-v1:expression
       (bnf-v1:alternatives-cons
         (bnf-v1:alternative $elements (fresh-test:span))
         (bnf-v1:alternatives-nil)) (fresh-test:span)) (fresh-test:span)))
(= (fresh-test:document $count $existing)
   (bnf-v1:document
     (bnf-v1:entries-cons
       (fresh-test:rule "s"
         (bnf-v1:elements-cons
           (bnf-v1:reference (bnf:text $existing) (fresh-test:span))
           (fresh-test:optionals $count)))
       (bnf-v1:entries-cons
         (fresh-test:rule $existing
           (bnf-v1:elements-cons (fresh-test:literal "z") (bnf-v1:elements-nil)))
         (bnf-v1:entries-nil))) (fresh-test:span)))

(= (fresh-test:origins (ebnf-v1:origins-nil)) 0)
(= (fresh-test:origins
     (ebnf-v1:origins-cons
       (ebnf-v1:helper-origin $name optional
         (ebnf-v1:rule-origin (bnf-v1:text-cons 115 (bnf-v1:text-nil)) (bnf-v1:source-span 0 0))
         (bnf-v1:source-span 0 0)) $tail))
   (+ 1 (fresh-test:origins $tail)))

; Admission independently rejects duplicate definitions and lexical collisions.
; It also checks that the original referenced definition remains reachable.
(= (fresh-test:observe $count $document $authority)
   (case (bnf:lower-ebnf-document $document $authority)
     (((EBNF:LoweredGrammar $original $policy $plain $origins)
        (case (bnf:validate-document $plain $policy)
          (((BNF:StructuredGrammarAdmitted $checked $checked-policy $validation)
             (EbnfHelperNamesV1 $count (fresh-test:origins $origins)))
           ($unexpected (EbnfHelperNamesRefusedV1 $count)))))
      ($unexpected (EbnfHelperNamesLoweringFailedV1 $count)))))

!(fresh-test:observe 129 (fresh-test:document 129 "##0") (bnf:default-authority "s"))
!(fresh-test:observe 257 (fresh-test:document 257 "##########0") (bnf:default-authority "s"))

; The lexical namespace participates in the bound even without a same-named
; authored grammar rule. Lexical class/label strings are retained as data.
!(let $lexical-name "############0"
   (let $authority
        (bnf:grammar-authority-from-string "s"
          (bnf-v1:lexical-environment
            (bnf-v1:lexical-declarations-cons
              (bnf-v1:lexical-declaration
                (bnf:text $lexical-name) "LongLexicalClassName"
                (bnf-v1:lexical-points (bnf-v1:scalars-cons 55 (bnf-v1:scalars-nil)))
                "long-lexical-label" (bnf-v1:lexical-origin "fixture" 0))
              (bnf-v1:lexical-declarations-nil))))
     (fresh-test:observe 33
       (bnf-v1:document
         (bnf-v1:entries-cons
           (fresh-test:rule "s"
             (bnf-v1:elements-cons
               (bnf-v1:reference (bnf:text $lexical-name) (fresh-test:span))
               (fresh-test:optionals 33)))
           (bnf-v1:entries-nil)) (fresh-test:span)) $authority)))
