; Rosetta Code "Arithmetic evaluation": parse with a grammar, build an AST,
; evaluate the AST. The parser is produced by lib_bnf from the EBNF file;
; the AST is built from the derivation and evaluated by ordinary equations.
!(import! &self lib/lib_bnf.metta)

; --- derivation -> AST ------------------------------------------------------
; An expression derivation is: ws term (ws addop ws term)* ws
(= (arith:ast (EBNF:Rule "expr" $rs $alt $as $in
       (EBNF:ElementsCons $ws1
         (EBNF:ElementsCons (EBNF:Reference "term" $s1 $i1 $term)
           (EBNF:ElementsCons (EBNF:Repetition $kind $id $s2 $i2 $iterations) $rest)))))
   (arith:fold-left (arith:ast $term) $iterations))

(= (arith:ast (EBNF:Rule "term" $rs $alt $as $in
       (EBNF:ElementsCons (EBNF:Reference "factor" $s1 $i1 $factor)
         (EBNF:ElementsCons (EBNF:Repetition $kind $id $s2 $i2 $iterations) $rest))))
   (arith:fold-left (arith:ast $factor) $iterations))

; factor alternative 0: a number; alternative 1: "(" expr ")"
(= (arith:ast (EBNF:Rule "factor" $rs 0 $as $in
       (EBNF:ElementsCons (EBNF:Reference "number" $s $i $number) $rest)))
   (arith:ast $number))
(= (arith:ast (EBNF:Rule "factor" $rs 1 $as $in
       (EBNF:ElementsCons $open
         (EBNF:ElementsCons (EBNF:Reference "expr" $s $i $expr) $rest))))
   (arith:ast $expr))

(= (arith:ast (EBNF:Rule "number" $rs $alt $as $in
       (EBNF:ElementsCons (EBNF:Repetition $kind $id $s $i $digits) $rest)))
   (Num (arith:digits 0 $digits)))

; each iteration of (ws op ws operand) extends the AST to the left
(= (arith:fold-left $acc (EBNF:IterationsNil)) $acc)
(= (arith:fold-left $acc
      (EBNF:IterationsCons
        (EBNF:Group $gid $gs $galt $gas $gin
          (EBNF:ElementsCons $ws1
            (EBNF:ElementsCons (EBNF:Reference $opname $os $oi $op)
              (EBNF:ElementsCons $ws2
                (EBNF:ElementsCons (EBNF:Reference $name $s $i $operand) $rest)))))
        $more))
   (arith:fold-left (Bin (arith:op $op) $acc (arith:ast $operand)) $more))

(= (arith:op (EBNF:Rule $name $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Literal $text $ls $li) $rest)))
   $text)

(= (arith:digits $acc (EBNF:IterationsNil)) $acc)
(= (arith:digits $acc
      (EBNF:IterationsCons
        (EBNF:Reference "digit" $s $i
          (EBNF:Rule "digit" $rs $alt $as $in $elems))
        $more))
   (arith:digits (+ (* $acc 10) $alt) $more))

; --- AST -> value -----------------------------------------------------------
(= (arith:eval (Num $n)) $n)
(= (arith:eval (Bin "+" $l $r)) (+ (arith:eval $l) (arith:eval $r)))
(= (arith:eval (Bin "-" $l $r)) (- (arith:eval $l) (arith:eval $r)))
(= (arith:eval (Bin "*" $l $r)) (* (arith:eval $l) (arith:eval $r)))
(= (arith:eval (Bin "/" $l $r)) (/ (arith:eval $l) (arith:eval $r)))

; --- driver -----------------------------------------------------------------
(= (arith:run $parser $text)
   (case (bnf:parse-ebnf-text $parser $text)
     (((EBNF:SourceParse (EBNF:Accepted $b $s (EBNF:DerivationsCons $tree (EBNF:DerivationsNil))) $gb $gs $low)
        (let $ast (arith:ast $tree) (Evaluated $text $ast (arith:eval $ast))))
      ((EBNF:SourceParse (EBNF:Accepted $b $s $forest) $gb $gs $low)
        (Ambiguous $text))
      ((EBNF:SourceParse (EBNF:Rejected $b $s $offset $expected) $gb $gs $low)
        (Rejected $text at $offset))
      ($other (Unexpected $text $other)))))

!(let $parser (bnf:parser-from-ebnf-file "examples/bnf/arithmetic_evaluation_v1.ebnf" "expr")
   (case $parser
     (((EBNF:SourceParser $handle $binding $source $receipt)
        (let $results
             ((arith:run $parser "(1+3)*7")
              (arith:run $parser "2*3 + 4/23 - 7")
              (arith:run $parser "(3 + 4) * 6-2+(4*(4))")
              (arith:run $parser "10 - 2 - 3")
              (arith:run $parser "-2")
              (arith:run $parser "3 +")
              (arith:run $parser "(4 + 3"))
          (let $_ (bnf:close-ebnf-parser $parser) $results)))
      ($other (ParserNotBuilt $other)))))
