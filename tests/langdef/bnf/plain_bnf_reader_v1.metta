!(import! &self ../../../lib/lib_bnf.metta)

(= (test:document-value
      (BNF:ReadValue
        (BNF:GrammarDocument $_binding $_source $document)))
   $document)

(= (test:counts (bnf-v1:entries-nil)) (0 0 0 0))
(= (test:counts
      (bnf-v1:entries-cons
        (bnf-v1:rule $_name $expression $_span) $tail))
   (let ($rules $comments $blanks $alternatives)
        (test:counts $tail)
     (let $current (test:alternative-count $expression)
       ((+ $rules 1) $comments $blanks (+ $alternatives $current)))))
(= (test:counts
      (bnf-v1:entries-cons
        (bnf-v1:comment $_text $_span) $tail))
   (let ($rules $comments $blanks $alternatives)
        (test:counts $tail)
     ($rules (+ $comments 1) $blanks $alternatives)))
(= (test:counts
      (bnf-v1:entries-cons
        (bnf-v1:blank $_span) $tail))
   (let ($rules $comments $blanks $alternatives)
        (test:counts $tail)
     ($rules $comments (+ $blanks 1) $alternatives)))

(= (test:alternative-count
      (bnf-v1:expression $alternatives $_span))
   (test:alternative-list-count $alternatives))
(= (test:alternative-list-count (bnf-v1:alternatives-nil)) 0)
(= (test:alternative-list-count
      (bnf-v1:alternatives-cons $_head $tail))
   (+ 1 (test:alternative-list-count $tail)))

(= (test:is-source-rejection
      (BNF:ReadRejected source-syntax $_offset $_expected))
   True)

!(bind! &read
   (bnf:read-file "tests/langdef/bnf/plain_bnf_examples_v1.bnf"))
!(bind! &document (test:document-value &read))

!(assertEqual
   (test:counts (bnf:document-entries &document))
   (4 1 0 8))

!(assertEqual
   (test:is-source-rejection
     (bnf:read-text "<broken> := \"x\"\n"))
   True)

!(assertEqual
   (test:alternative-count
     (bnf:rule-expression
       (bnf-v1:rule
         (bnf-v1:text-cons 97 (bnf-v1:text-nil))
         (bnf-v1:expression
           (bnf-v1:alternatives-cons
             (bnf-v1:alternative
               (bnf-v1:elements-nil)
               (bnf-v1:source-span 0 0))
             (bnf-v1:alternatives-nil))
           (bnf-v1:source-span 0 0))
         (bnf-v1:source-span 0 0))))
   1)

; The self-hosting authority must be available through the library itself,
; independent of a host's rule for resolving nested import paths.
(= (test:authority-start
      (bnf-v1:grammar-authority
        (bnf-v1:start $name) (bnf-v1:lexical-environment $_lexicals)))
   (bnf:text->string $name))

!(assertEqual (test:authority-start (bnf:self-authority-v1)) "document")

!(println! (PlainBnfReaderV1Summary 4 4 0))
