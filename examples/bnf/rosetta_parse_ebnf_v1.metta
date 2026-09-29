; Rosetta Code "Parse EBNF": read a grammar written in the task's EBNF flavor,
; then parse inputs according to that grammar. lib_bnf reads the flavor's own
; grammar (rosetta_ebnf_v1.ebnf), the derivation is lowered to lib_bnf's
; postfix EBNF text, and lib_bnf builds the parser for the described language.
!(import! &self lib/lib_bnf.metta)

; --- code-point text helpers (bnf:text values) -------------------------------
(= (ros:append (bnf-v1:text-nil) $ys) $ys)
(= (ros:append (bnf-v1:text-cons $c $xs) $ys) (bnf-v1:text-cons $c (ros:append $xs $ys)))
(= (ros:cat ()) (bnf-v1:text-nil))
(= (ros:cat ($x)) $x)
(= (ros:cat ($x $y)) (ros:append $x $y))
(= (ros:cat ($x $y $z)) (ros:append $x (ros:append $y $z)))
(= (ros:cat ($x $y $z $w)) (ros:append $x (ros:append $y (ros:append $z $w))))
(= (ros:cat ($x $y $z $w $v)) (ros:append $x (ros:append $y (ros:append $z (ros:append $w $v)))))

; --- lexical classes of the flavor -------------------------------------------
(= (ros:scalars ()) (bnf-v1:scalars-nil))
(= (ros:scalars ($c $rest)) (bnf-v1:scalars-cons $c (ros:scalars $rest)))
(= (ros:authority)
   (bnf:grammar-authority-from-string "syntax"
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration (bnf:text "idchar") "RosettaIdChar"
           (bnf-v1:lexical-except
             (ros:scalars (9 (10 (13 (32 (34 (39 (40 (41 (46 (59 (61 (91 (93 (123 (124 (125 ()))))))))))))))))))
           "rosetta-idchar" (bnf-v1:lexical-origin "rosetta-ebnf-v1" 0))
         (bnf-v1:lexical-declarations-cons
           (bnf-v1:lexical-declaration (bnf:text "dqchar") "RosettaDoubleQuotedChar"
             (bnf-v1:lexical-except (ros:scalars (10 (13 (34 ())))))
             "rosetta-dqchar" (bnf-v1:lexical-origin "rosetta-ebnf-v1" 1))
           (bnf-v1:lexical-declarations-cons
             (bnf-v1:lexical-declaration (bnf:text "sqchar") "RosettaSingleQuotedChar"
               (bnf-v1:lexical-except (ros:scalars (10 (13 (39 ())))))
               "rosetta-sqchar" (bnf-v1:lexical-origin "rosetta-ebnf-v1" 2))
             (bnf-v1:lexical-declarations-nil)))))))

; --- derivation -> lib_bnf postfix EBNF text ----------------------------------
; syntax: ws title? "{" ws production* "}" ws comment?
(= (ros:lower-syntax (EBNF:Rule "syntax" $rs $alt $as $in
      (EBNF:ElementsCons $ws0 (EBNF:ElementsCons $title (EBNF:ElementsCons $open
        (EBNF:ElementsCons $ws1 (EBNF:ElementsCons (EBNF:Repetition $k $id $s $i $productions) $rest)))))))
   (ros:lower-productions $productions))

(= (ros:lower-productions (EBNF:IterationsNil)) (bnf-v1:text-nil))
(= (ros:lower-productions (EBNF:IterationsCons (EBNF:Reference "production" $s $i $production) $more))
   (ros:append (ros:lower-production $production) (ros:lower-productions $more)))

; production: identifier ws "=" ws expression ws terminator ws
(= (ros:lower-production (EBNF:Rule "production" $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Reference "identifier" $s1 $i1 $identifier)
        (EBNF:ElementsCons $ws1 (EBNF:ElementsCons $eq (EBNF:ElementsCons $ws2
          (EBNF:ElementsCons (EBNF:Reference "expression" $s2 $i2 $expression) $rest)))))))
   (ros:cat ((bnf:text "<") (ros:identifier $identifier) (bnf:text "> ::= ")
             (ros:lower-expression $expression) (bnf:text "\n"))))

; expression: term (ws "|" ws term)*
(= (ros:lower-expression (EBNF:Rule "expression" $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Reference "term" $s $i $term)
        (EBNF:ElementsCons (EBNF:Repetition $k $id $s2 $i2 $more) $rest))))
   (ros:append (ros:lower-term $term) (ros:lower-alternatives $more)))
(= (ros:lower-alternatives (EBNF:IterationsNil)) (bnf-v1:text-nil))
(= (ros:lower-alternatives (EBNF:IterationsCons
      (EBNF:Group $g $gs $ga $gas $gi
        (EBNF:ElementsCons $ws1 (EBNF:ElementsCons $bar (EBNF:ElementsCons $ws2
          (EBNF:ElementsCons (EBNF:Reference "term" $s $i $term) $rest)))))
      $more))
   (ros:cat ((bnf:text " | ") (ros:lower-term $term) (ros:lower-alternatives $more))))

; term: factor term-tail; term-tail: | ws1 identifier term-tail | ws delimited term-tail
(= (ros:lower-term (EBNF:Rule "term" $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Reference "factor" $s $i $factor)
        (EBNF:ElementsCons (EBNF:Reference "term-tail" $s2 $i2 $tail) $rest))))
   (ros:append (ros:lower-factor $factor) (ros:lower-tail $tail)))
(= (ros:lower-tail (EBNF:Rule "term-tail" $rs 0 $as $in $elems)) (bnf-v1:text-nil))
(= (ros:lower-tail (EBNF:Rule "term-tail" $rs 1 $as $in
      (EBNF:ElementsCons $ws (EBNF:ElementsCons (EBNF:Reference "identifier" $s $i $identifier)
        (EBNF:ElementsCons (EBNF:Reference "term-tail" $s2 $i2 $tail) $rest)))))
   (ros:cat ((bnf:text " <") (ros:identifier $identifier) (bnf:text ">") (ros:lower-tail $tail))))
(= (ros:lower-tail (EBNF:Rule "term-tail" $rs 2 $as $in
      (EBNF:ElementsCons $ws (EBNF:ElementsCons (EBNF:Reference "delimited" $s $i $delimited)
        (EBNF:ElementsCons (EBNF:Reference "term-tail" $s2 $i2 $tail) $rest)))))
   (ros:cat ((bnf:text " ") (ros:lower-delimited $delimited) (ros:lower-tail $tail))))

; factor: identifier | delimited
(= (ros:lower-factor (EBNF:Rule "factor" $rs 0 $as $in
      (EBNF:ElementsCons (EBNF:Reference "identifier" $s $i $identifier) $rest)))
   (ros:cat ((bnf:text "<") (ros:identifier $identifier) (bnf:text ">"))))
(= (ros:lower-factor (EBNF:Rule "factor" $rs 1 $as $in
      (EBNF:ElementsCons (EBNF:Reference "delimited" $s $i $delimited) $rest)))
   (ros:lower-delimited $delimited))

; delimited: literal | [e] | (e) | {e}
; A literal consumes the whitespace that follows it, which is how a token
; grammar becomes a scannerless one without ambiguity.
(= (ros:lower-delimited (EBNF:Rule "delimited" $rs 0 $as $in
      (EBNF:ElementsCons (EBNF:Reference "literal" $s $i $literal) $rest)))
   (ros:cat ((bnf:text "\"") (ros:literal $literal) (bnf:text "\" <ws>"))))
(= (ros:lower-delimited (EBNF:Rule "delimited" $rs 1 $as $in
      (EBNF:ElementsCons $open (EBNF:ElementsCons $ws1
        (EBNF:ElementsCons (EBNF:Reference "expression" $s $i $expression) $rest)))))
   (ros:cat ((bnf:text "(") (ros:lower-expression $expression) (bnf:text ")?"))))
(= (ros:lower-delimited (EBNF:Rule "delimited" $rs 2 $as $in
      (EBNF:ElementsCons $open (EBNF:ElementsCons $ws1
        (EBNF:ElementsCons (EBNF:Reference "expression" $s $i $expression) $rest)))))
   (ros:cat ((bnf:text "(") (ros:lower-expression $expression) (bnf:text ")"))))
(= (ros:lower-delimited (EBNF:Rule "delimited" $rs 3 $as $in
      (EBNF:ElementsCons $open (EBNF:ElementsCons $ws1
        (EBNF:ElementsCons (EBNF:Reference "expression" $s $i $expression) $rest)))))
   (ros:cat ((bnf:text "(") (ros:lower-expression $expression) (bnf:text ")*"))))

; identifier: idchar+  (lexical leaves carry their code point)
(= (ros:identifier (EBNF:Rule "identifier" $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Repetition $k $id $s $i $chars) $rest)))
   (ros:lexical-text $chars))
(= (ros:lexical-text (EBNF:IterationsNil)) (bnf-v1:text-nil))
(= (ros:lexical-text (EBNF:IterationsCons
      (EBNF:Reference $name $s $i (EBNF:Lexical $n $token $origin $li $codepoint)) $more))
   (bnf-v1:text-cons $codepoint (ros:lexical-text $more)))

; literal: "dqchar*" | 'sqchar*'  -> escaped body for a lib_bnf literal
(= (ros:literal (EBNF:Rule "literal" $rs $alt $as $in
      (EBNF:ElementsCons $quote (EBNF:ElementsCons (EBNF:Repetition $k $id $s $i $chars) $rest))))
   (ros:escaped $chars))
(= (ros:escaped (EBNF:IterationsNil)) (bnf-v1:text-nil))
(= (ros:escaped (EBNF:IterationsCons
      (EBNF:Reference $name $s $i (EBNF:Lexical $n $token $origin $li $codepoint)) $more))
   (ros:escape-one $codepoint (ros:escaped $more)))
(= (ros:escape-one $c $tail)
   (case $c ((34 (bnf-v1:text-cons 92 (bnf-v1:text-cons 34 $tail)))
             (92 (bnf-v1:text-cons 92 (bnf-v1:text-cons 92 $tail)))
             ($other (bnf-v1:text-cons $c $tail)))))

; first production name = start symbol of the described language
(= (ros:start-name (EBNF:Rule "syntax" $rs $alt $as $in
      (EBNF:ElementsCons $ws0 (EBNF:ElementsCons $title (EBNF:ElementsCons $open
        (EBNF:ElementsCons $ws1 (EBNF:ElementsCons
          (EBNF:Repetition $k $id $s $i
            (EBNF:IterationsCons (EBNF:Reference "production" $ps $pi
              (EBNF:Rule "production" $prs $palt $pas $pin
                (EBNF:ElementsCons (EBNF:Reference "identifier" $is $ii $identifier) $prest)))
              $more))
          $rest)))))))
   (ros:identifier $identifier))

; the whole lowered grammar: a start rule that skips leading whitespace, the
; productions, and the whitespace rule every literal consumes after itself
(= (ros:lowered-grammar $syntax)
   (bnf:text->string
     (ros:cat ((bnf:text "<rosetta-start> ::= <ws> <") (ros:start-name $syntax) (bnf:text ">\n")
               (ros:lower-syntax $syntax)
               (bnf:text "<ws> ::= (\" \" | \"\\t\" | \"\\n\" | \"\\r\")*\n")))))

; --- driver -------------------------------------------------------------------
(= (ros:check $parser $input)
   (case (bnf:parse-ebnf-text $parser $input)
     (((EBNF:SourceParse (EBNF:Accepted $b $s $forest) $gb $gs $low) (valid $input))
      ((EBNF:SourceParse (EBNF:Rejected $b $s $offset $expected) $gb $gs $low) (invalid $input at $offset))
      ($other (unexpected $input)))))
(= (ros:check-all $parser ()) ())
(= (ros:check-all $parser ($x $rest)) (ros:check $parser $x (ros:check-all $parser $rest)))
(= (ros:check $parser $x $tail) ((ros:check $parser $x) $tail))

; Read a Rosetta grammar with the flavor's parser, lower it, build the
; described language's parser, and classify the inputs.
(= (ros:run $reader $label $grammar-text $inputs)
   (case (bnf:parse-ebnf-text $reader $grammar-text)
     (((EBNF:SourceParse (EBNF:Accepted $b $s (EBNF:DerivationsCons $syntax (EBNF:DerivationsNil))) $gb $gs $low)
        (let $lowered (ros:lowered-grammar $syntax)
          (case (bnf:parser-from-ebnf-text $lowered "rosetta-start")
            (((EBNF:SourceParser $h $pb $ps $receipt)
               (let $results (ros:results $lowered (bnf:parser-from-ebnf-text $lowered "rosetta-start") $inputs)
                 (Grammar $label lowered-to $lowered $results)))
             ($diagnostics (Grammar $label rejected-by-admission $diagnostics))))))
      ((EBNF:SourceParse (EBNF:Rejected $b $s $offset $expected) $gb $gs $low)
        (Grammar $label malformed-ebnf at $offset))
      ((EBNF:SourceParse (EBNF:Incomplete $b $s $code $detail) $gb $gs $low)
        (Grammar $label reader-incomplete $detail))
      ($other (Grammar $label unexpected)))))
(= (ros:results $lowered $parser $inputs)
   (let $r (ros:check-list $parser $inputs)
     (let $_ (bnf:close-ebnf-parser $parser) $r)))
(= (ros:check-list $parser ()) ())
(= (ros:check-list $parser ($x $rest))
   (let $first (ros:check $parser $x)
     (let $others (ros:check-list $parser $rest)
       ($first $others))))

!(let $reader (bnf:parser-from-ebnf-file-with-authority "examples/bnf/rosetta_ebnf_v1.ebnf" (ros:authority))
   (case $reader
     (((EBNF:SourceParser $h $b $s $receipt)
        (let $one-liner
             (ros:run $reader "one-liner"
               "\"a\" {\n    a = \"a1\" ( \"a2\" | \"a3\" ) { \"a4\" } [ \"a5\" ] \"a6\" ;\n} \"z\""
               ("a1a3a4a4a5a6" (" a1 a2a6 " ("a1 a3 a4 a6" ("a1 a4 a5 a6" ("a1 a2 a4 a5 a5 a6" ("a1 a2 a4 a5 a6 a7" ("your ad here" ()))))))))
          (let $arith
               (ros:run $reader "arithmetic"
                 "{\n    expr = term { plus term } .\n    term = factor { times factor } .\n    factor = number | '(' expr ')' .\n\n    plus = \"+\" | \"-\" .\n    times = \"*\" | \"/\" .\n\n    number = digit { digit } .\n    digit = \"0\" | \"1\" | \"2\" | \"3\" | \"4\" | \"5\" | \"6\" | \"7\" | \"8\" | \"9\" .\n}"
                 ("2" ("2*3 + 4/23 - 7" ("(3 + 4) * 6-2+(4*(4))" ("-2" ("3 +" ("(4 + 3" ())))))))
            (let $bad1 (ros:run $reader "no-braces" "a = \"1\";" ())
              (let $bad2 (ros:run $reader "unterminated" "{ a = \"1\" ;" ())
                (let $bad3 (ros:run $reader "space-in-identifier" "{ hello world = \"1\"; }" ())
                  (let $bad4 (ros:run $reader "undefined-nonterminal" "{ foo = bar . }" ())
                    (let $_ (bnf:close-ebnf-parser $reader)
                      ($one-liner $arith $bad1 $bad2 $bad3 $bad4)))))))))
      ($other (ReaderNotBuilt $other)))))
