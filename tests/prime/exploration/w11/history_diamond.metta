; Histories with one answer.  A transition system with a diamond and a
; shortcut: start moves to left or to right, each of these moves to done,
; and start also moves to done directly.  A history is the list of states a
; run visits from start.  Keeping only its last state is the answer view.
;
; A consumer of histories descends to the answer view when it can be computed
; from the answer alone.  A split, two histories with one answer and
; different values, rules that out (NonTrivialFiber.not_factors); having no
; split is descent (function_descends_iff).  The program looks for a split
; among all histories from start.
; Values are bound with let before a list builtin reads them: a builtin
; that takes an atom reads an expression written in its argument as syntax.

(edge start left)
(edge start right)
(edge left done)
(edge right done)
(edge start done)

; Every history from a state: stop there, or take an edge and go on.
(= (history-from $s) ($s))
(= (history-from $s)
   (let $next (match &self (edge $s $n) $n)
     (let $rest (history-from $next) (cons-atom $s $rest))))

(= (last $xs)
   (let $tail (cdr-atom $xs)
     (if (== $tail ()) (car-atom $xs) (last $tail))))
(= (member $x $xs)
   (if (== $xs ()) False
       (let $head (car-atom $xs)
         (if (== $head $x) True
             (let $tail (cdr-atom $xs) (member $x $tail))))))

; Four consumers of a history.
(= (answer $h) (last $h))                 ; the state it reached
(= (cost $h) (- (size-atom $h) 1))        ; the number of steps it took
(= (provenance $h) $h)                    ; the states it visited
(= (passes-left $h) (member left $h))     ; whether it went through left

; A split of a consumer, and descent as the absence of splits.
(= (split $consumer)
   (let* (($x (history-from start))
          ($y (history-from start)))
     (if (and (== (answer $x) (answer $y))
              (not (== ($consumer $x) ($consumer $y))))
         ($x $y)
         (empty))))
(= (descends $consumer)
   (let $splits (collapse (split $consumer)) (== $splits ())))

(= (via-left) (start left done))
(= (via-right) (start right done))
(= (direct) (start done))

; 1. The three histories that reach done have one answer (same_answer).
!(let* (($l (via-left)) ($r (via-right)) ($d (direct)))
   (and (== (answer $l) (answer $d)) (== (answer $l) (answer $r))))
; 2. The answer consumer descends (answer_descends).
!(descends answer)
; 3. The cost consumer does not (cost_not_descends): through a branch the run
;    takes two steps, along the shortcut one.
!(descends cost)
!(let* (($l (via-left)) ($d (direct))) (== (cost $l) (cost $d)))
; 4. Provenance does not descend either, even between the two histories of
;    equal cost (provenance_not_descends, equal_cost).
!(descends provenance)
!(let* (($l (via-left)) ($r (via-right))) (== (cost $l) (cost $r)))
; 5. Nor does the consumer "went through left" (passesLeft_not_descends).
!(descends passes-left)
