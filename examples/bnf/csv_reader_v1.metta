; CSV (RFC 4180) read from a file through lib_bnf: rows become MeTTa lists of strings,
; quoted fields are unescaped, and a numeric column is summed by equations.
!(import! &self lib/lib_bnf.metta)

(= (csv:append (bnf-v1:text-nil) $ys) $ys)
(= (csv:append (bnf-v1:text-cons $c $xs) $ys) (bnf-v1:text-cons $c (csv:append $xs $ys)))

(= (csv:authority)
   (bnf:grammar-authority-from-string "file"
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration (bnf:text "qchar") "CsvQuotedChar"
           (bnf-v1:lexical-except (bnf-v1:scalars-cons 34 (bnf-v1:scalars-nil)))
           "csv-qchar" (bnf-v1:lexical-origin "csv-v1" 0))
         (bnf-v1:lexical-declarations-cons
           (bnf-v1:lexical-declaration (bnf:text "uchar") "CsvUnquotedChar"
             (bnf-v1:lexical-except
               (bnf-v1:scalars-cons 10 (bnf-v1:scalars-cons 13 (bnf-v1:scalars-cons 34
                 (bnf-v1:scalars-cons 44 (bnf-v1:scalars-nil))))))
             "csv-uchar" (bnf-v1:lexical-origin "csv-v1" 1))
           (bnf-v1:lexical-declarations-nil))))))

; --- derivation -> rows -----------------------------------------------------------
(= (csv:rows (EBNF:Rule "file" $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Reference "record" $s $i $record)
        (EBNF:ElementsCons (EBNF:Repetition $k $id $s2 $i2 $more) $rest))))
   (cons-atom (csv:record $record) (csv:more-rows $more)))
(= (csv:more-rows (EBNF:IterationsNil)) ())
(= (csv:more-rows (EBNF:IterationsCons
      (EBNF:Group $g $gs $ga $gas $gi
        (EBNF:ElementsCons $newline (EBNF:ElementsCons (EBNF:Reference "record" $s $i $record) $rest)))
      $more))
   (cons-atom (csv:record $record) (csv:more-rows $more)))

(= (csv:record (EBNF:Rule "record" $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Reference "field" $s $i $field)
        (EBNF:ElementsCons (EBNF:Repetition $k $id $s2 $i2 $more) $rest))))
   (cons-atom (csv:field $field) (csv:more-fields $more)))
(= (csv:more-fields (EBNF:IterationsNil)) ())
(= (csv:more-fields (EBNF:IterationsCons
      (EBNF:Group $g $gs $ga $gas $gi
        (EBNF:ElementsCons $comma (EBNF:ElementsCons (EBNF:Reference "field" $s $i $field) $rest)))
      $more))
   (cons-atom (csv:field $field) (csv:more-fields $more)))

(= (csv:field (EBNF:Rule "field" $rs 0 $as $in (EBNF:ElementsCons (EBNF:Reference "quoted" $s $i $q) $rest)))
   (bnf:text->string (csv:quoted $q)))
(= (csv:field (EBNF:Rule "field" $rs 1 $as $in (EBNF:ElementsCons (EBNF:Reference "unquoted" $s $i $u) $rest)))
   (bnf:text->string (csv:unquoted $u)))

(= (csv:quoted (EBNF:Rule "quoted" $rs $alt $as $in
      (EBNF:ElementsCons $open (EBNF:ElementsCons (EBNF:Repetition $k $id $s $i $chars) $rest))))
   (csv:quoted-chars $chars))
(= (csv:quoted-chars (EBNF:IterationsNil)) (bnf-v1:text-nil))
; group alternative 0 is a plain character, alternative 1 is a doubled quote
(= (csv:quoted-chars (EBNF:IterationsCons (EBNF:Group $g $gs 0 $gas $gi
      (EBNF:ElementsCons (EBNF:Reference "qchar" $s $i (EBNF:Lexical $n $t $o $li $codepoint)) $rest)) $more))
   (bnf-v1:text-cons $codepoint (csv:quoted-chars $more)))
(= (csv:quoted-chars (EBNF:IterationsCons (EBNF:Group $g $gs 1 $gas $gi $elems) $more))
   (bnf-v1:text-cons 34 (csv:quoted-chars $more)))

(= (csv:unquoted (EBNF:Rule "unquoted" $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Repetition $k $id $s $i $chars) $rest)))
   (csv:unquoted-chars $chars))
(= (csv:unquoted-chars (EBNF:IterationsNil)) (bnf-v1:text-nil))
(= (csv:unquoted-chars (EBNF:IterationsCons
      (EBNF:Reference "uchar" $s $i (EBNF:Lexical $n $t $o $li $codepoint)) $more))
   (bnf-v1:text-cons $codepoint (csv:unquoted-chars $more)))

; --- work on the rows -----------------------------------------------------------
(= (csv:column $rows $index)
   (if (== (size-atom $rows) 0) ()
     (cons-atom (csv:nth (car-atom $rows) $index) (csv:column (cdr-atom $rows) $index))))
(= (csv:nth $items $n) (if (== $n 0) (car-atom $items) (csv:nth (cdr-atom $items) (- $n 1))))
(= (csv:sum $values) (if (== (size-atom $values) 0) 0 (+ (parse (car-atom $values)) (csv:sum (cdr-atom $values)))))

(= (csv:read $parser $path)
   (case (bnf:parse-ebnf-file $parser $path)
     (((EBNF:SourceParse (EBNF:Accepted $b $s (EBNF:DerivationsCons $file (EBNF:DerivationsNil))) $gb $gs $low)
        (csv:rows $file))
      ((EBNF:SourceParse (EBNF:Rejected $b $s $offset $expected) $gb $gs $low) (Rejected at $offset))
      ($other (Unexpected)))))

!(let $parser (bnf:parser-from-ebnf-file-with-authority "examples/bnf/csv_v1.ebnf" (csv:authority))
   (case $parser
     (((EBNF:SourceParser $h $b $s $receipt)
        (let $rows (csv:read $parser "examples/bnf/csv_sample_v1.csv")
          (let $results
               ((rows $rows)
                (row-count (size-atom $rows))
                (quantities (csv:column (cdr-atom $rows) 1))
                (total-quantity (csv:sum (csv:column (cdr-atom $rows) 1)))
                (unbalanced-quote (csv:read $parser "examples/bnf/csv_unbalanced_v1.csv")))
            (let $_ (bnf:close-ebnf-parser $parser) $results))))
      ($other (ParserNotBuilt $other)))))
