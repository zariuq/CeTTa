; Lists in equations the open-equation tier compiles: PeTTa's `cons` as a
; head pattern, a let or case pattern and a constructor, met by flat lists,
; cons cells, unbound variables, the empty list and non-lists.  The gate
; runs it on both routes and compares with SWI-PeTTa's answers.

; A head pattern: a flat list gives its first element and the rest, which
; shares its storage; a cons cell its fields; an unbound argument is bound
; to a cell; the empty list and a symbol fail.
(= (first-of (cons $h $t)) $h)
(= (rest-of (cons $h $t)) $t)
!(first-of (a b c))
!(rest-of (a b c))
!(rest-of (cons x (y z)))
!(collapse (first-of ()))
!(collapse (first-of foo))
!(let $r (first-of $x) (let (q w) $x $r))

; Nested patterns and a pattern over two elements.
(= (second-of (cons $h (cons $s $t))) $s)
!(second-of (1 2 3))
!(collapse (second-of (1)))
(= (swap-pair (pair (cons $a $rest) $b)) (pair $b (cons $a $rest)))
!(swap-pair (pair (1 2) 3))

; A constructor in a body, and its value met by patterns.
(= (push $x $l) (cons $x $l))
!(push 1 (2 3))
!(push 1 ())
!(let (cons $h $t) (push 1 (2 3)) ($h $t))
!(let (cons $h $t) (a b c) ($h $t))
!(collapse (let (cons $h $t) () $h))
!(collapse (let (cons $h $t) foo $h))

; A list pattern in a case.
(= (describe $l)
   (case $l
     (((cons $h ()) (one $h))
      ((cons $h $t) (many $h))
      ($other none))))
!(describe (a))
!(describe (a b))
!(describe ())

; Cells inside ground values unify with the flat lists they spell.
(= (same $x $x) yes)
!(same (f (cons 1 ())) (f (1)))
!(collapse (same (f (cons 1 ())) (f (2))))

; A functional queue, as PeTTa's library writes one.
(= (enqueue $e (queue $in $out $n)) (queue (cons $e $in) $out (+ $n 1)))
(= (dequeue $e (queue $in (cons $e $out) $n)) (queue $in $out (- $n 1)))
(= (dequeue $e (queue $in () $n))
   (let (cons $e $r) (reverse $in) (queue () $r (- $n 1))))
(= (fill $k $q) (if (< $k 1) $q (fill (- $k 1) (enqueue $k $q))))
(= (drain $q $acc)
   (if (== $q (queue () () 0))
       $acc
       (let $q1 (once (dequeue $x $q)) (drain $q1 (+ $acc $x)))))
!(drain (fill 300 (queue () () 0)) 0)

; A match takes its pattern as written: a spelled `cons` there is data.
(item (cons a b))
!(match &self (item (cons $h $t)) ($h $t))
