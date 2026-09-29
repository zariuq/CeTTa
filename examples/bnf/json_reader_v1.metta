; JSON (RFC 8259) read through lib_bnf: the grammar file produces the parser,
; the reader returns the canonical term of the grammar's derived LanguageDef,
; written (bnf NAME child...) so grammar names never stand where an evaluator
; could reduce them; one equation per constructor turns it into MeTTa
; values, and ordinary
; equations query the result.  The grammar writes its layout as the rule ws,
; which the value equations ignore; numbers are printed back by the grammar's
; own printer and read by `parse`.
!(import! &self lib/lib_bnf.metta)

; --- text helpers ---------------------------------------------------------------
(= (json:append (bnf-v1:text-nil) $ys) $ys)
(= (json:append (bnf-v1:text-cons $c $xs) $ys) (bnf-v1:text-cons $c (json:append $xs $ys)))

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

; --- canonical term -> value -----------------------------------------------------
(= (json:value $parser (bnf object/0 $ws)) (Object ()))
(= (json:value $parser (bnf object/1 $ws $member $more $ws2))
   (Object (cons-atom (json:member $parser $member) (json:members $parser $more))))
(= (json:value $parser (bnf array/0 $ws)) (Array ()))
(= (json:value $parser (bnf array/1 $ws $item $more $ws2))
   (Array (cons-atom (json:value $parser $item) (json:items $parser $more))))
(= (json:value $parser (bnf string $chars)) (bnf:text->string (json:chars $chars)))
(= (json:value $parser (bnf number $sign $int $frac $exp))
   (parse (bnf:print-canonical $parser "number" (bnf number $sign $int $frac $exp))))
(= (json:value $parser "true") True)
(= (json:value $parser "false") False)
(= (json:value $parser "null") Null)

; the repetitions ("," member)* and ("," value)* are lists of (ws ws x) tuples
(= (json:last ($ws1 $ws2 $x)) $x)
(= (json:members $parser $more)
   (if (== (size-atom $more) 0) ()
     (cons-atom (json:member $parser (json:last (car-atom $more)))
                (json:members $parser (cdr-atom $more)))))
(= (json:items $parser $more)
   (if (== (size-atom $more) 0) ()
     (cons-atom (json:value $parser (json:last (car-atom $more)))
                (json:items $parser (cdr-atom $more)))))
(= (json:member $parser (bnf member $key $ws1 $ws2 $value))
   ((json:value $parser $key) (json:value $parser $value)))

; strings: unescape into code points, then to a MeTTa string
(= (json:chars $chars)
   (if (== (size-atom $chars) 0) (bnf-v1:text-nil)
     (json:append (json:char (car-atom $chars)) (json:chars (cdr-atom $chars)))))
(= (json:char (bnf strchar $s)) (bnf:text $s))
(= (json:char (bnf char $escape))
   (case $escape
     (((bnf escape $a $b $c $d)
        (bnf-v1:text-cons
          (+ (* 4096 (json:hex $a)) (+ (* 256 (json:hex $b)) (+ (* 16 (json:hex $c)) (json:hex $d))))
          (bnf-v1:text-nil)))
      ($letter (json:letter $letter)))))
; a one-character escape is decoded by its code point; ", \ and / stand for themselves
(= (json:letter $letter)
   (case (bnf:text $letter)
     (((bnf-v1:text-cons 98 $end) (bnf-v1:text-cons 8 (bnf-v1:text-nil)))
      ((bnf-v1:text-cons 102 $end) (bnf-v1:text-cons 12 (bnf-v1:text-nil)))
      ((bnf-v1:text-cons 110 $end) (bnf-v1:text-cons 10 (bnf-v1:text-nil)))
      ((bnf-v1:text-cons 114 $end) (bnf-v1:text-cons 13 (bnf-v1:text-nil)))
      ((bnf-v1:text-cons 116 $end) (bnf-v1:text-cons 9 (bnf-v1:text-nil)))
      ($same $same))))
(= (json:hex $digit)
   (case (bnf:text $digit) (((bnf-v1:text-cons $d $rest) (json:hex-digit $d)))))
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
   (case (bnf:parse-canonical-text $parser $text)
     (((EBNF:Canonical $b $s (bnf json $ws1 $value $ws2)) (json:value $parser $value))
      ((EBNF:Rejected $b $s $offset $expected) (Rejected at $offset))
      ((EBNF:Incomplete $b $s $code $detail) (Incomplete $detail))
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
