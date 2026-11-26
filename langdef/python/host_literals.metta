; What a Python literal's text denotes (the Python Language Reference, 2.4),
; for the AST stage's host relations (AstHost.metta).  On CeTTa's PeTTa lane:
; booleans are true and false; a character list from str:codepoints is a
; List value, measured with size-atom.

!(import! &self str)

; ---------------------------------------------------------------- lists

(= (pyhost:empty $xs) (== (size-atom $xs) 0))
(= (pyhost:drop $n $xs)
   (if (== $n 0) $xs (let ($h $t) (decons-atom $xs) (pyhost:drop (- $n 1) $t))))
(= (pyhost:take $n $xs)
   (if (== $n 0) () (let ($h $t) (decons-atom $xs) (cons-atom $h (pyhost:take (- $n 1) $t)))))
(= (pyhost:member $x $xs)
   (if (pyhost:empty $xs) false
     (let ($h $t) (decons-atom $xs) (if (== $h $x) true (pyhost:member $x $t)))))

; ---------------------------------------------------------------- numbers

(= (pyhost:digit "0") 0) (= (pyhost:digit "1") 1) (= (pyhost:digit "2") 2) (= (pyhost:digit "3") 3)
(= (pyhost:digit "4") 4) (= (pyhost:digit "5") 5) (= (pyhost:digit "6") 6) (= (pyhost:digit "7") 7)
(= (pyhost:digit "8") 8) (= (pyhost:digit "9") 9)
(= (pyhost:digit "a") 10) (= (pyhost:digit "b") 11) (= (pyhost:digit "c") 12)
(= (pyhost:digit "d") 13) (= (pyhost:digit "e") 14) (= (pyhost:digit "f") 15)
(= (pyhost:digit "A") 10) (= (pyhost:digit "B") 11) (= (pyhost:digit "C") 12)
(= (pyhost:digit "D") 13) (= (pyhost:digit "E") 14) (= (pyhost:digit "F") 15)

; the value of DIGITS (one-character strings) in BASE, after ACC
(= (pyhost:base-value $base $digits $acc)
   (if (pyhost:empty $digits) $acc
     (let ($d $rest) (decons-atom $digits)
       (pyhost:base-value $base $rest (+ (* $acc $base) (pyhost:digit $d))))))

(= (pyhost:prefixed $t $lower $upper)
   (if (str:starts-with $t $lower) true (str:starts-with $t $upper)))

; a decimal literal's value; a float beyond the largest double is infinite
; (Python reads 1e400 as inf), which the PeTTa lane writes as the number inf
(= (pyhost:decimal $t)
   (case (str:string->number $t)
     (((Error $call $why) inf)
      ($x $x))))

; an imaginary literal is the complex number 0 + Xj, X a float
(= (pyhost:imaginary $t)
   (let $cs (str:codepoints $t)
     (let $x (pyhost:decimal (str:join "" (pyhost:take (- (size-atom $cs) 1) $cs)))
       (py:Complex 0.0 (if (== $x inf) inf (* 1.0 $x))))))

(= (pyhost:number $text)
   (let $t (str:join "" (str:split "_" $text))
     (if (pyhost:prefixed $t "0x" "0X") (pyhost:base-value 16 (pyhost:drop 2 (str:codepoints $t)) 0)
       (if (pyhost:prefixed $t "0o" "0O") (pyhost:base-value 8 (pyhost:drop 2 (str:codepoints $t)) 0)
         (if (pyhost:prefixed $t "0b" "0B") (pyhost:base-value 2 (pyhost:drop 2 (str:codepoints $t)) 0)
           (if (str:ends-with $t "j") (pyhost:imaginary $t)
             (if (str:ends-with $t "J") (pyhost:imaginary $t)
               (pyhost:decimal $t))))))))

; ---------------------------------------------------------------- strings

; a literal's prefix letters, and its characters from the first quote
(= (pyhost:split-prefix $cs $prefix)
   (let ($c $rest) (decons-atom $cs)
     (if (== $c "'") ($prefix $cs)
       (if (== $c "\"") ($prefix $cs)
         (pyhost:split-prefix $rest (cons-atom $c $prefix))))))

; whether a quoted text opens with three quotes
(= (pyhost:triple $cs)
   (if (< (size-atom $cs) 6) false
     (let $open (str:join "" (pyhost:take 3 $cs))
       (if (== $open "'''") true (== $open "\"\"\"")))))

; the characters between the quotes
(= (pyhost:body $cs)
   (let $n (size-atom $cs)
     (if (pyhost:triple $cs)
         (pyhost:take (- $n 6) (pyhost:drop 3 $cs))
         (pyhost:take (- $n 2) (pyhost:drop 1 $cs)))))

(= (pyhost:has-letter $prefix $lower $upper)
   (if (pyhost:member $lower $prefix) true (pyhost:member $upper $prefix)))

; a bytes literal's characters are ASCII as written (escapes give the rest);
; any other has no value (a SyntaxError)
(= (pyhost:string $text)
   (let ($prefix $quoted) (pyhost:split-prefix (str:codepoints $text) ())
     (let $body (pyhost:body $quoted)
       (let $mode (if (pyhost:has-letter $prefix "b" "B") bytes str)
         (if (and (== $mode bytes) (not (str:is-all-in ascii (str:join "" $body)))) (empty)
           (let $chars (if (pyhost:has-letter $prefix "r" "R") $body (pyhost:unescape $mode $body))
             (if (== $mode bytes)
                 (py:Bytes (str:join "" $chars))
                 (str:join "" $chars))))))))

(= (pyhost:string-kind $text)
   (let ($prefix $quoted) (pyhost:split-prefix (str:codepoints $text) ())
     (if (pyhost:has-letter $prefix "u" "U") "u" (py:None))))

; adjacent literals of one kind join; a bytes literal beside a str literal
; joins to nothing (a SyntaxError)
(= (pyhost:concat $a $b)
   (case ($a $b)
     ((((py:Bytes $x) (py:Bytes $y)) (py:Bytes (str:concat $x $y)))
      (((py:Bytes $x) $y) (empty))
      (($x (py:Bytes $y)) (empty))
      (($x $y) (str:concat $x $y)))))

; ---------------------------------------------------------------- f-strings

; whether an f-string's opener is raw
(= (pyhost:fstring-raw $start)
   (if (pyhost:has-letter (str:codepoints $start) "r" "R") (py:True) (py:False)))
; an interpolated string's kind by its opener: a template string (PEP 750) or
; a formatted one
(= (pyhost:fstring-kind $start)
   (if (pyhost:has-letter (str:codepoints $start) "t" "T") (py:tstr) (py:fstr)))
; a field's source text without its comments (CPython rebuilds a field's
; text from its tokens): from a "#" outside a string literal to the line's
; end, the newline kept
(= (pyhost:strip-comments $t) (str:join "" (pyhost:sc-code (str:codepoints $t))))
(= (pyhost:sc-code $cs)
   (if (== (size-atom $cs) 0) ()
     (let ($c $rest) (decons-atom $cs)
       (if (== $c "#") (pyhost:sc-comment $rest)
         (if (if (== $c "'") true (== $c "\""))
             (if (pyhost:sc-two $c $rest)
                 (cons-atom $c (cons-atom $c (cons-atom $c (pyhost:sc-string $c true (pyhost:sc-drop2 $rest)))))
                 (cons-atom $c (pyhost:sc-string $c false $rest)))
             (cons-atom $c (pyhost:sc-code $rest)))))))
(= (pyhost:sc-comment $cs)
   (if (== (size-atom $cs) 0) ()
     (let ($c $rest) (decons-atom $cs)
       (if (== $c "\n") (cons-atom $c (pyhost:sc-code $rest)) (pyhost:sc-comment $rest)))))
; inside a string literal opened by the quote Q (three of them when TRIPLE):
; a backslash keeps the character after it
(= (pyhost:sc-string $q $triple $cs)
   (if (== (size-atom $cs) 0) ()
     (let ($c $rest) (decons-atom $cs)
       (if (== $c "\\")
           (if (== (size-atom $rest) 0) (cons-atom $c ())
             (let ($d $rest2) (decons-atom $rest) (cons-atom $c (cons-atom $d (pyhost:sc-string $q $triple $rest2)))))
           (if (== $c $q)
               (if $triple
                   (if (pyhost:sc-two $q $rest)
                       (cons-atom $q (cons-atom $q (cons-atom $q (pyhost:sc-code (pyhost:sc-drop2 $rest)))))
                       (cons-atom $c (pyhost:sc-string $q $triple $rest)))
                   (cons-atom $c (pyhost:sc-code $rest)))
               (cons-atom $c (pyhost:sc-string $q $triple $rest)))))))
(= (pyhost:sc-two $q $cs)
   (if (< (size-atom $cs) 2) false
     (let ($a $r1) (decons-atom $cs) (if (== $a $q) (let ($b $r2) (decons-atom $r1) (== $b $q)) false))))
(= (pyhost:sc-drop2 $cs) (let ($a $r1) (decons-atom $cs) (let ($b $r2) (decons-atom $r1) $r2)))
; a debug field's expression text ({x = } shows "x = "; the expression's own
; text is what comes before that last "=")
(= (pyhost:debug-text $t) (str:join "" (pyhost:first-chars (str:codepoints $t) (str:rfind-index "=" $t))))
(= (pyhost:first-chars $cs $n)
   (if (<= $n 0) () (let ($c $rest) (decons-atom $cs) (cons-atom $c (pyhost:first-chars $rest (- $n 1))))))

; an f-string's text piece: a doubled bracket at its end stands for one; the
; rest is a string literal's body, raw or not; an empty piece is (py:Empty)
(= (pyhost:fstring-middle $text $raw)
   (let $chars (pyhost:undouble (str:codepoints $text))
     (let $decoded (if (== $raw (py:True)) $chars (pyhost:unescape str $chars))
       (let $s (str:join "" $decoded)
         (if (== $s "") (py:Empty) (py:Text $s))))))

(= (pyhost:undouble $cs)
   (let $n (size-atom $cs)
     (if (< $n 2) $cs
       (let $last (str:join "" (pyhost:drop (- $n 2) $cs))
         (if (== $last "{{") (pyhost:take (- $n 1) $cs)
           (if (== $last "}}") (pyhost:take (- $n 1) $cs) $cs))))))

; a conversion's letter (!r !s !a) as ast numbers it: its character code
(= (pyhost:conversion "r") 114)
(= (pyhost:conversion "s") 115)
(= (pyhost:conversion "a") 97)

; ---------------------------------------------------------------- the rest

; the constants the abstract syntax states, by name
(= (pyhost:const (py:zero)) 0)
(= (pyhost:const (py:one)) 1)
(= (pyhost:const (py:three)) 3)
(= (pyhost:const (py:minus-one)) -1)
(= (pyhost:const (py:repr)) 114)
(= (pyhost:const (py:star)) "*")

; a capture pattern's name: _ captures nothing
(= (pyhost:capture $t) (if (== $t "_") (py:None) $t))

(= (pyhost:is-empty $v) (if (== $v "") (py:True) (py:False)))
(= (pyhost:is-future $m) (if (== $m "__future__") (py:True) (py:False)))
(= (pyhost:add $a $b) (+ $a $b))
(= (pyhost:dotted $a $b) (str:concat (str:concat $a ".") $b))

; escape sequences (2.4.1.1): a backslash and what follows it.  MODE is str or
; bytes; \u, \U and \N{...} are escapes of str literals only.
(= (pyhost:unescape $mode $cs)
   (if (pyhost:empty $cs) ()
     (let ($c $rest) (decons-atom $cs)
       (if (== $c "\\") (pyhost:escape $mode $rest) (cons-atom $c (pyhost:unescape $mode $rest))))))

; a code point of a str: any up to U+10FFFF, a lone surrogate included (Python
; strings are code point sequences; generalized UTF-8 holds them)
(= (pyhost:char $code) (str:from-codepoints-generalized ($code)))

; a code point written with N hex digits after the escape letter
(= (pyhost:hex-escape $mode $n $rest)
   (cons-atom (pyhost:char (pyhost:base-value 16 (pyhost:take $n $rest) 0))
              (pyhost:unescape $mode (pyhost:drop $n $rest))))

(= (pyhost:escape $mode $cs)
   (if (pyhost:empty $cs) ("\\")
     (let ($e $rest) (decons-atom $cs)
       (case ($mode $e)
         (((str "u") (pyhost:hex-escape $mode 4 $rest))
          ((str "U") (pyhost:hex-escape $mode 8 $rest))
          (($m "x") (pyhost:hex-escape $mode 2 $rest))
          (($m "\n") (pyhost:unescape $mode $rest))
          (($m "\\") (cons-atom "\\" (pyhost:unescape $mode $rest)))
          (($m "'") (cons-atom "'" (pyhost:unescape $mode $rest)))
          (($m "\"") (cons-atom "\"" (pyhost:unescape $mode $rest)))
          (($m "a") (cons-atom (pyhost:char 7) (pyhost:unescape $mode $rest)))
          (($m "b") (cons-atom (pyhost:char 8) (pyhost:unescape $mode $rest)))
          (($m "f") (cons-atom (pyhost:char 12) (pyhost:unescape $mode $rest)))
          (($m "n") (cons-atom (pyhost:char 10) (pyhost:unescape $mode $rest)))
          (($m "r") (cons-atom (pyhost:char 13) (pyhost:unescape $mode $rest)))
          (($m "t") (cons-atom (pyhost:char 9) (pyhost:unescape $mode $rest)))
          (($m "v") (cons-atom (pyhost:char 11) (pyhost:unescape $mode $rest)))
          (($m $other)
            (if (pyhost:octal-digit $other)
                (pyhost:octal $mode $cs)
                (cons-atom "\\" (cons-atom $other (pyhost:unescape $mode $rest))))))))))

(= (pyhost:octal-digit $c) (pyhost:member $c ("0" "1" "2" "3" "4" "5" "6" "7")))

; up to three octal digits
(= (pyhost:octal $mode $cs)
   (let $n (pyhost:octal-run $cs 0)
     (cons-atom (pyhost:char (pyhost:base-value 8 (pyhost:take $n $cs) 0))
                (pyhost:unescape $mode (pyhost:drop $n $cs)))))
(= (pyhost:octal-run $cs $n)
   (if (== $n 3) 3
     (if (pyhost:empty $cs) $n
       (let ($c $rest) (decons-atom $cs)
         (if (pyhost:octal-digit $c) (pyhost:octal-run $rest (+ $n 1)) $n)))))
