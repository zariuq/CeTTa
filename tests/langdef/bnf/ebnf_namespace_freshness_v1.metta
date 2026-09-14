!(import! &self ../../../lib/lib_bnf.metta)

; These checks observe non-capture, not any chosen helper spelling or count.
; An unresolved source reference must not become resolved by a generated helper.
(= (namespace-test:span) (bnf-v1:source-span 0 0))
(= (namespace-test:literal)
   (bnf-v1:literal (bnf:text "x") (namespace-test:span)))
(= (namespace-test:expression $body)
   (bnf-v1:expression
     (bnf-v1:alternatives-cons
       (bnf-v1:alternative
         (bnf-v1:elements-cons $body (bnf-v1:elements-nil))
         (namespace-test:span))
       (bnf-v1:alternatives-nil)) (namespace-test:span)))
(= (namespace-test:rule $name $body)
   (bnf-v1:rule (bnf:text $name) (namespace-test:expression $body)
     (namespace-test:span)))
(= (namespace-test:doc $tail $body)
   (bnf-v1:document
     (bnf-v1:entries-cons (namespace-test:rule "s" $body) $tail)
     (namespace-test:span)))
(= (namespace-test:optional)
   (ebnf-v1:optional (namespace-test:literal) (namespace-test:span)))
(= (namespace-test:fresh $occupied (ebnf-v1:origins-nil)) True)
(= (namespace-test:fresh $occupied
     (ebnf-v1:origins-cons
       (ebnf-v1:helper-origin $name $kind $owner $span) $tail))
   (if (== $occupied $name) False (namespace-test:fresh $occupied $tail)))
(= (namespace-test:probe $label $document $authority)
   (case (bnf:lower-ebnf-document $document $authority)
     (((EBNF:LoweredGrammar $original $policy $plain $origins)
        (EbnfNamespaceFreshV1 $label (namespace-test:fresh (bnf:text "##0") $origins)))
      ($failure (EbnfNamespaceFailureV1 $label $failure)))))

!(namespace-test:probe declaration
   (namespace-test:doc
     (bnf-v1:entries-cons (namespace-test:rule "##0" (namespace-test:literal))
       (bnf-v1:entries-nil)) (namespace-test:optional))
   (bnf:default-authority "s"))

!(namespace-test:probe nested-reference
   (namespace-test:doc (bnf-v1:entries-nil)
     (ebnf-v1:optional
       (ebnf-v1:group
         (namespace-test:expression
           (bnf-v1:reference (bnf:text "##0") (namespace-test:span)))
         (namespace-test:span)) (namespace-test:span)))
   (bnf:default-authority "s"))

!(namespace-test:probe lexical
   (namespace-test:doc (bnf-v1:entries-nil) (namespace-test:optional))
   (bnf:grammar-authority-from-string "s"
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration (bnf:text "##0") "k"
           (bnf-v1:lexical-points (bnf-v1:scalars-cons 55 (bnf-v1:scalars-nil)))
           "l" (bnf-v1:lexical-origin "fixture" 0))
         (bnf-v1:lexical-declarations-nil)))))
