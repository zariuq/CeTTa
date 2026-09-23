; JSON (RFC 8259) read through lib_bnf: the grammar file produces the parser,
; the derivation is converted to MeTTa values, and ordinary equations query
; the result. Numbers are read by `parse` from their matched text.
!(import! &self lib/lib_bnf.metta)

; --- text helpers ---------------------------------------------------------------
(= (json:append (bnf-v1:text-nil) $ys) $ys)
(= (json:append (bnf-v1:text-cons $c $xs) $ys) (bnf-v1:text-cons $c (json:append $xs $ys)))

; the concatenated leaf text of any derivation subtree
(= (json:leaf-text (EBNF:Literal $text $s $i)) (bnf:text $text))
(= (json:leaf-text (EBNF:Lexical $n $token $origin $i $codepoint)) (bnf-v1:text-cons $codepoint (bnf-v1:text-nil)))
(= (json:leaf-text (EBNF:Reference $n $s $i $sub)) (json:leaf-text $sub))
(= (json:leaf-text (EBNF:Rule $n $rs $alt $as $in $elems)) (json:leaf-texts $elems))
(= (json:leaf-text (EBNF:Group $id $s $alt $as $in $elems)) (json:leaf-texts $elems))
(= (json:leaf-text (EBNF:OptionalPresent $id $s $i $e)) (json:leaf-text $e))
(= (json:leaf-text (EBNF:OptionalAbsent $id $s $i)) (bnf-v1:text-nil))
(= (json:leaf-text (EBNF:Repetition $k $id $s $i $its)) (json:iteration-texts $its))
(= (json:leaf-texts (EBNF:ElementsNil)) (bnf-v1:text-nil))
(= (json:leaf-texts (EBNF:ElementsCons $e $rest)) (json:append (json:leaf-text $e) (json:leaf-texts $rest)))
(= (json:iteration-texts (EBNF:IterationsNil)) (bnf-v1:text-nil))
(= (json:iteration-texts (EBNF:IterationsCons $e $rest)) (json:append (json:leaf-text $e) (json:iteration-texts $rest)))

; --- lexical class for string characters ----------------------------------------
(= (json:controls $n)
   (if (< $n 32)
     (bnf-v1:scalars-cons $n (json:controls (+ $n 1)))
     (bnf-v1:scalars-cons 34 (bnf-v1:scalars-cons 92 (bnf-v1:scalars-nil)))))
(= (json:authority)
   (bnf:grammar-authority-from-string "json"
     (bnf-v1:lexical-environment
       (bnf-v1:lexical-declarations-cons
         (bnf-v1:lexical-declaration (bnf:text "strchar") "JsonStringChar"
           (bnf-v1:lexical-except (json:controls 0))
           "json-strchar" (bnf-v1:lexical-origin "json-v1" 0))
         (bnf-v1:lexical-declarations-nil)))))

; --- derivation -> value ---------------------------------------------------------
(= (json:value (EBNF:Rule "value" $rs 0 $as $in (EBNF:ElementsCons (EBNF:Reference "object" $s $i $o) $r))) (json:object $o))
(= (json:value (EBNF:Rule "value" $rs 1 $as $in (EBNF:ElementsCons (EBNF:Reference "array" $s $i $a) $r))) (json:array $a))
(= (json:value (EBNF:Rule "value" $rs 2 $as $in (EBNF:ElementsCons (EBNF:Reference "string" $s $i $str) $r))) (json:string $str))
(= (json:value (EBNF:Rule "value" $rs 3 $as $in (EBNF:ElementsCons (EBNF:Reference "number" $s $i $num) $r)))
   (parse (bnf:text->string (json:leaf-text $num))))
(= (json:value (EBNF:Rule "value" $rs 4 $as $in $e)) True)
(= (json:value (EBNF:Rule "value" $rs 5 $as $in $e)) False)
(= (json:value (EBNF:Rule "value" $rs 6 $as $in $e)) Null)

(= (json:object (EBNF:Rule "object" $rs 0 $as $in $e)) (Object ()))
(= (json:object (EBNF:Rule "object" $rs 1 $as $in
      (EBNF:ElementsCons $open (EBNF:ElementsCons $ws
        (EBNF:ElementsCons (EBNF:Reference "member" $s $i $m)
          (EBNF:ElementsCons (EBNF:Repetition $k $id $s2 $i2 $more) $rest))))))
   (Object (cons-atom (json:member $m) (json:members $more))))
(= (json:members (EBNF:IterationsNil)) ())
(= (json:members (EBNF:IterationsCons
      (EBNF:Group $g $gs $ga $gas $gi
        (EBNF:ElementsCons $ws1 (EBNF:ElementsCons $comma (EBNF:ElementsCons $ws2
          (EBNF:ElementsCons (EBNF:Reference "member" $s $i $m) $rest)))))
      $more))
   (cons-atom (json:member $m) (json:members $more)))
(= (json:member (EBNF:Rule "member" $rs $alt $as $in
      (EBNF:ElementsCons (EBNF:Reference "string" $s $i $key)
        (EBNF:ElementsCons $ws1 (EBNF:ElementsCons $colon (EBNF:ElementsCons $ws2
          (EBNF:ElementsCons (EBNF:Reference "value" $s2 $i2 $v) $rest)))))))
   ((json:string $key) (json:value $v)))

(= (json:array (EBNF:Rule "array" $rs 0 $as $in $e)) (Array ()))
(= (json:array (EBNF:Rule "array" $rs 1 $as $in
      (EBNF:ElementsCons $open (EBNF:ElementsCons $ws
        (EBNF:ElementsCons (EBNF:Reference "value" $s $i $v)
          (EBNF:ElementsCons (EBNF:Repetition $k $id $s2 $i2 $more) $rest))))))
   (Array (cons-atom (json:value $v) (json:values $more))))
(= (json:values (EBNF:IterationsNil)) ())
(= (json:values (EBNF:IterationsCons
      (EBNF:Group $g $gs $ga $gas $gi
        (EBNF:ElementsCons $ws1 (EBNF:ElementsCons $comma (EBNF:ElementsCons $ws2
          (EBNF:ElementsCons (EBNF:Reference "value" $s $i $v) $rest)))))
      $more))
   (cons-atom (json:value $v) (json:values $more)))

; strings: unescape into code points, then to a MeTTa string
(= (json:string (EBNF:Rule "string" $rs $alt $as $in
      (EBNF:ElementsCons $open (EBNF:ElementsCons (EBNF:Repetition $k $id $s $i $chars) $rest))))
   (bnf:text->string (json:chars $chars)))
(= (json:chars (EBNF:IterationsNil)) (bnf-v1:text-nil))
(= (json:chars (EBNF:IterationsCons (EBNF:Reference "char" $s $i $c) $more))
   (json:append (json:char $c) (json:chars $more)))
(= (json:char (EBNF:Rule "char" $rs 0 $as $in $e)) (json:leaf-texts $e))
(= (json:char (EBNF:Rule "char" $rs 1 $as $in
      (EBNF:ElementsCons $backslash (EBNF:ElementsCons (EBNF:Reference "escape" $s $i $esc) $rest))))
   (json:escape $esc))
(= (json:escape (EBNF:Rule "escape" $rs $alt $as $in $e))
   (case $alt
     ((0 (bnf-v1:text-cons 34 (bnf-v1:text-nil)))
      (1 (bnf-v1:text-cons 92 (bnf-v1:text-nil)))
      (2 (bnf:text "/"))
      (3 (bnf-v1:text-cons 8 (bnf-v1:text-nil)))
      (4 (bnf-v1:text-cons 12 (bnf-v1:text-nil)))
      (5 (bnf:text "\n"))
      (6 (bnf:text "\r"))
      (7 (bnf:text "\t"))
      (8 (bnf-v1:text-cons (json:hex-value 0 (json:leaf-texts $e)) (bnf-v1:text-nil))))))
; the leaf text of the \uXXXX alternative is "uXXXX": skip the u, fold the digits
(= (json:hex-value $acc (bnf-v1:text-nil)) $acc)
(= (json:hex-value $acc (bnf-v1:text-cons $d $rest))
   (if (== $d 117) (json:hex-value $acc $rest)
     (json:hex-value (+ (* $acc 16) (json:hex-digit $d)) $rest)))
(= (json:hex-digit $d)
   (if (< $d 58) (- $d 48) (if (< $d 71) (- $d 55) (- $d 87))))

; --- queries over the value ---------------------------------------------------
; members and items are flat MeTTa lists, so walk them with car/cdr
(= (json:get (Object $members) $key) (json:lookup $members $key))
(= (json:lookup $members $key)
   (if (== (size-atom $members) 0) (Missing $key)
     (let $m (car-atom $members)
       (if (== (car-atom $m) $key) (car-atom (cdr-atom $m)) (json:lookup (cdr-atom $members) $key)))))
(= (json:at (Array $items) $index) (json:nth $items $index))
(= (json:nth $items $n) (if (== $n 0) (car-atom $items) (json:nth (cdr-atom $items) (- $n 1))))
(= (json:length (Array $items)) (size-atom $items))

; --- driver ---------------------------------------------------------------------
(= (json:read $parser $text)
   (case (bnf:parse-ebnf-text $parser $text)
     (((EBNF:SourceParse (EBNF:Accepted $b $s (EBNF:DerivationsCons
          (EBNF:Rule "json" $rs $alt $as $in
            (EBNF:ElementsCons $ws1 (EBNF:ElementsCons (EBNF:Reference "value" $vs $vi $v) $rest)))
          (EBNF:DerivationsNil))) $gb $gs $low)
        (json:value $v))
      ((EBNF:SourceParse (EBNF:Rejected $b $s $offset $expected) $gb $gs $low) (Rejected at $offset))
      ((EBNF:SourceParse (EBNF:Incomplete $b $s $code $detail) $gb $gs $low) (Incomplete $detail))
      ($other (Ambiguous)))))

!(let $parser (bnf:parser-from-ebnf-file-with-authority "examples/bnf/json_v1.ebnf" (json:authority))
   (case $parser
     (((EBNF:SourceParser $h $b $s $receipt)
        (let $doc (json:read $parser "{\n  \"name\": \"Oru\\u017ei \\u0022lib_bnf\\u0022\\tok\",\n  \"version\": 1.5e2,\n  \"tags\": [\"metta\", \"ebnf\", null, true, false, -0.25],\n  \"nested\": {\"depth\": [[1], [2, [3]]], \"empty\": {}, \"none\": []}\n}")
          (let $results
               ((document $doc)
                (name (json:get $doc "name"))
                (version (json:get $doc "version"))
                (third-tag (json:at (json:get $doc "tags") 2))
                (tag-count (json:length (json:get $doc "tags")))
                (deep (json:at (json:at (json:get (json:get $doc "nested") "depth") 1) 1))
                (missing (json:get $doc "absent"))
                (bad-trailing-comma (json:read $parser "[1, 2,]"))
                (bad-leading-zero (json:read $parser "007"))
                (bad-unquoted-key (json:read $parser "{a: 1}")))
            (let $_ (bnf:close-ebnf-parser $parser) $results))))
      ($other (ParserNotBuilt $other)))))
