!(import! &self ../../../lib/lib_bnf.metta)

; <params> has three list-making alternatives: typed and untyped parameters
; differ in their elements, spaced and comma-terminated ones only in their
; separators.  Canonical terms name each list, so all three read to distinct
; terms and print back to their own text; the collision report says which
; list names a morphism could not erase.  The typed-lambda grammar has no
; collision; the ambiguous fixture reads its one literal text two ways.
; car-atom and cdr-atom are total in PeTTa, () for (), so the walk stops by
; its own test rather than by a second equation for ().
(= (collisions-test:shapes $collisions)
   (if (== $collisions ())
       ()
       (let $head (car-atom $collisions)
         (let $tail (cdr-atom $collisions)
           (let $rest (collisions-test:shapes $tail)
             (case $head
               (((EBNF:Collision $kind $sort $_first $_second)
                  (cons-atom (Shape $kind $sort) $rest))
                ((EBNF:Collision $kind $sort $_rule)
                  (cons-atom (Shape $kind $sort) $rest)))))))))

(= (collisions-test:terms $parser $text)
   (case (bnf:parse-canonical-text $parser $text)
     (((EBNF:Canonical $_binding $_source $term) $term)
      ($other (NotRead $text)))))

(= (collisions-test:run $path $start)
   (let $parser (bnf:parser-from-ebnf-file $path $start)
     (let $collisions (bnf:canonical-collisions $parser)
       (let $_ (bnf:close-ebnf-parser $parser)
         (EbnfCollisionsV1 $start (collisions-test:shapes $collisions))))))

!(collisions-test:run "tests/langdef/bnf/ebnf_collisions_v1.ebnf" "decl")
(= (collisions-test:again $parser $text)
   (let $term (collisions-test:terms $parser $text)
     (== (bnf:print-canonical $parser "decl" $term) $text)))

!(let $parser (bnf:parser-from-ebnf-file "tests/langdef/bnf/ebnf_collisions_v1.ebnf" "decl")
   (let $typed (collisions-test:terms $parser "fun x:o y:i .")
     (let $spaced (collisions-test:terms $parser "fun x y .")
       (let $commas (collisions-test:terms $parser "fun x,y,.")
         (let $printed (EbnfCollisionsV1 prints-back
                         (collisions-test:again $parser "fun x:o y:i .")
                         (collisions-test:again $parser "fun x y .")
                         (collisions-test:again $parser "fun x,y,."))
           (let $_ (bnf:close-ebnf-parser $parser)
             (EbnfCollisionsV1 distinct
               (== $typed $spaced) (== $spaced $commas) (== $typed $commas)
               $spaced $commas $printed)))))))
!(collisions-test:run "tests/langdef/bnf/ebnf_binders_v1.ebnf" "term")
!(collisions-test:run "tests/langdef/bnf/native-equivalence/grammars/ambiguous.ebnf" "s")
