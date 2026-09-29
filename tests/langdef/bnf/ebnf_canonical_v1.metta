!(import! &self ../../../lib/lib_bnf.metta)

; Canonical terms of lib_bnf grammars.  Every accepted input reads as one
; canonical term, prints back to exactly its own text (the same source
; digest), and reads again to the same term; rejected input is not a term.

(= (canon:controls $n)
   (if (< $n 32)
     (bnf-v1:scalars-cons $n (canon:controls (+ $n 1)))
     (bnf-v1:scalars-cons 34 (bnf-v1:scalars-cons 92 (bnf-v1:scalars-nil)))))
(= (canon:json-authority)
   (bnf:grammar-authority-from-string "json"
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration (bnf:text "strchar") "JsonStringChar"
           (bnf-v1:lexical-except (canon:controls 0))
           "json-strchar" (bnf-v1:lexical-origin "json-v1" 0))
         (bnf-v1:lexical-declarations-nil)))))

; Every check answers with a symbol, so the same expectation serves the HE
; and the PeTTa lane, which spell their booleans differently.
(= (canon:flag $holds) (if $holds ok no))

; Print a read term and read it again: the same source and the same term.
(= (canon:again $parser $start $source $term)
   (let $printed (bnf:print-canonical $parser $start $term)
     (case (bnf:parse-canonical-text $parser $printed)
       (((EBNF:Canonical $binding $again-source $again)
          (canon:flag (and (== $again-source $source) (== $again $term))))
        ($other no)))))

(= (canon:round-trip $parser $start $text)
   (case (bnf:parse-canonical-text $parser $text)
     (((EBNF:Canonical $binding $source $term) (canon:again $parser $start $source $term))
      ($other no))))

(= (canon:rejected $parser $text)
   (case (bnf:parse-canonical-text $parser $text)
     (((EBNF:Rejected $binding $source $offset $expected) rejected)
      ($other accepted))))

(= (canon:json $parser)
   (EbnfCanonicalV1 json
     ((canon:round-trip $parser "json" "[1, \"a\\n\", {\"k\": -2.5e3}, true]")
      (canon:round-trip $parser "json" " {\"name\": \"Oru\\u017ei\", \"xs\": [0, -1, 2E+10]} ")
      (canon:round-trip $parser "json" "null")
      (canon:round-trip $parser "json" "{\"nested\": {\"depth\": [[1], [2, [3]]], \"empty\": {}}}")
      (canon:rejected $parser "[1, 2,]")
      (canon:rejected $parser "007"))))

(= (canon:grammar-file $parser $path)
   (case (bnf:parse-canonical-file $parser $path)
     (((EBNF:Canonical $binding $source $term)
        (canon:again $parser "document" $source $term))
      ($other no))))

(= (canon:grammars $parser)
   (EbnfCanonicalV1 grammars
     ((canon:grammar-file $parser "langdef/bnf/ebnf_self_v1.ebnf")
      (canon:grammar-file $parser "examples/bnf/json_v1.ebnf")
      (canon:grammar-file $parser "examples/bnf/csv_v1.ebnf")
      (canon:grammar-file $parser "examples/bnf/arithmetic_evaluation_v1.ebnf")
      (canon:rejected $parser "<a> ::= <b\n"))))

;  An ambiguous sort: two derivations of one text read to two terms, each a
;  constructor named by its alternative, and each prints back to the text.
(= (canon:ambiguous $grammar $text)
   (let $parser (bnf:parser-from-ebnf-text $grammar "s")
     (case (bnf:parse-canonical-text $parser $text)
       (((EBNF:CanonicalAmbiguous $binding $source ($left $right))
          (let $result
               (EbnfCanonicalV1 ambiguous $left $right
                 (canon:flag (not (== $left $right)))
                 (canon:flag (== (bnf:print-canonical $parser "s" $left) $text))
                 (canon:flag (== (bnf:print-canonical $parser "s" $right) $text)))
            (let $_ (bnf:close-ebnf-parser $parser) $result)))
        ($other (let $_ (bnf:close-ebnf-parser $parser) (EbnfCanonicalV1 ambiguous $other)))))))

(= (canon:language $parser)
   (case (bnf:canonical-language $parser)
     (((GSLTLanguageDefWireV1 $name $types (LCons $row $rows) $equations $rewrites) ok)
      ($other no))))

!(let $parser (bnf:parser-from-ebnf-file-with-authority
                "examples/bnf/json_v1.ebnf" (canon:json-authority))
   (let $result (canon:json $parser)
     (let $language (canon:language $parser)
       (let $_ (bnf:close-ebnf-parser $parser) ($result (EbnfCanonicalV1 language $language))))))

!(let $parser (bnf:parser-from-ebnf-file-with-authority
                "langdef/bnf/ebnf_self_v1.ebnf" (bnf:self-authority-v1))
   (let $result (canon:grammars $parser)
     (let $_ (bnf:close-ebnf-parser $parser) $result)))

!(canon:ambiguous "<s> ::= \"ab\" | \"a\" \"b\"\n" "ab")
!(canon:ambiguous "<s> ::= \"x\" | \"x\"\n" "x")
