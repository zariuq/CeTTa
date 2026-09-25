!(import! &self ../../../lib/lib_bnf.metta)

; Binder declarations on the canonical terms of a grammar: a lambda declares
; typed variables in a telescope, and let binds its variable in its body but
; not in the term it names.  The rewrite below matches every application;
; binding operations take terms as data, so it never fires on them.
(= (bnf term/3 $function $argument) BOOM)

(= (binders-test:signature $parser)
   (bnf:binding-signature $parser (AbtNames (StringInitial "A" "Z"))
     ((Binds term/2 (Fields declarations body)
        (BindTelescope declarations (Scope body)
          (Annotated (Head bnf declaration) 2 3)))
      (Binds term/4 (Fields variable named body)
        (BindPattern variable (Scope body))))))

(= (binders-test:term $parser $text)
   (case (bnf:parse-canonical-text $parser $text)
     (((EBNF:Canonical $_binding $_source $term) $term)
      ($other (NotRead $text $other)))))

(= (binders-test:free $parser $signature $text)
   (let $term (binders-test:term $parser $text)
     (BindersV1 free $text (langdef:free-names $signature $term))))

(= (binders-test:substitute $parser $signature $bindings $text)
   (let $term (binders-test:term $parser $text)
     (let $result (langdef:substitute-names $signature $bindings $term)
       (BindersV1 substitute $text
         (bnf:print-canonical $parser "term" $result)))))

(= (binders-test:apart $parser $signature $text)
   (let $term (binders-test:term $parser $text)
     (let $result (langdef:rename-apart $signature () $term)
       (BindersV1 apart $text (bnf:print-canonical $parser "term" $result)))))

(= (binders-test:alpha $parser $signature $left $right)
   (let $l (binders-test:term $parser $left)
     (let $r (binders-test:term $parser $right)
       (BindersV1 alpha $left $right (langdef:alpha-equal $signature $l $r)))))

(= (binders-test:run $parser)
   (let $signature (binders-test:signature $parser)
     (BindersV1Results
       (binders-test:free $parser $signature "\\X:o.(X Y)")
       (binders-test:free $parser $signature "\\X:o,Y:(o>i).(X let Z = Y in (Z a))")
       (binders-test:free $parser $signature "let X = Y in (X Z)")
       (binders-test:free $parser $signature "let X = X in X")
       (binders-test:substitute $parser $signature (("Z" "X")) "\\X:o.(X Z)")
       (binders-test:substitute $parser $signature (("X" "a")) "(X let X = b in X)")
       (binders-test:apart $parser $signature "(\\X:o.X \\X:i.X)")
       (binders-test:alpha $parser $signature "\\X:o.X" "\\Y:o.Y")
       (binders-test:alpha $parser $signature "\\X:o,Y:o.(X Y)" "\\Y:o,X:o.(X Y)")
       (binders-test:alpha $parser $signature "let X = a in X" "let Y = a in Y")
       (BindersV1 refused
         (bnf:binding-signature $parser (AbtNames (StringInitial "A" "Z"))
           ((Binds term/9 (Fields a b) (BindPattern a (Scope b)))))
         (bnf:binding-signature $parser (AbtNames (StringInitial "A" "Z"))
           ((Binds term/2 (Fields body) (BindPattern body (Scope body)))))))))

!(let $parser (bnf:parser-from-ebnf-file "tests/langdef/bnf/ebnf_binders_v1.ebnf" "term")
   (case $parser
     (((EBNF:SourceParser $handle $binding $source $receipt)
        (let $results (binders-test:run $parser)
          (let $_ (bnf:close-ebnf-parser $parser) $results)))
      ($other (BindersParserNotBuilt $other)))))
