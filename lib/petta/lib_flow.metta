; Imperative flows for PeTTa: a sequence of statements, a loop over the items
; of a collection, a loop over the matches in a store, a loop while a
; condition holds, and a loop that repeats a step until it is quiet.
;
; Every statement of a flow runs for its effects: it runs to completion (all
; of its answers, every branch, with their effects) and its answers are
; dropped.  Statements and bodies are held, and evaluated when the flow
; reaches them, every time it reaches them.
;
;   (flow:then s rest)            s runs to completion, then rest gives the
;                                 answers; a block s1; s2; e is
;                                 (flow:then s1 (flow:then s2 e)).
;   (flow:for-each e f)           e runs to completion as the loop starts; then
;                                 (f x) runs to completion for each answer x
;                                 of e, in order.  The answer is ().  A list
;                                 of items is looped over as the answers of
;                                 (superpose (a b c)): superpose holds its list
;                                 and runs each item, so the list is never a
;                                 call of its first item, while a bare
;                                 (a b c) in a running position would be.
;   (flow:for-match s p body)     body runs to completion once for each match
;                                 of p in s, with p's variables bound; the
;                                 matches are those of the store when the loop
;                                 starts.  The answer is ().
;   (flow:while c body)           while one of the answers of c is True, body
;                                 runs to completion; c is evaluated again
;                                 before each round.  The answer is ().
;   (flow:until-quiet step)       step runs to completion again and again
;                                 until a round has no answer (for a
;                                 transform: until nothing fires).  The answer
;                                 is the number of rounds that had answers.

(: flow:then (-> Atom Atom %Undefined%))
(= (flow:then $s $rest) (let $_ (collapse (eval $s)) (eval $rest)))

(: flow:for-each (-> Atom Atom %Undefined%))
(= (flow:for-each $e $f) (flow:for-answers (collapse (eval $e)) $f))
(: flow:for-answers (-> %Undefined% Atom %Undefined%))
(= (flow:for-answers $answers $f)
   (if (== $answers ()) ()
       (let $_ (collapse (eval ($f (car-atom $answers))))
         (flow:for-answers (cdr-atom $answers) $f))))

(: flow:for-match (-> %Undefined% Atom Atom %Undefined%))
(= (flow:for-match $space $pattern $body)
   (let $_ (collapse (match $space $pattern (eval $body))) ()))

(: flow:while (-> Atom Atom %Undefined%))
(= (flow:while $cond $body)
   (if (flow:holds $cond)
       (let $_ (collapse (eval $body)) (flow:while $cond $body))
       ()))
(: flow:holds (-> Atom %Undefined%))
(= (flow:holds $cond) (flow:has-true (collapse (eval $cond))))
(= (flow:has-true $answers)
   (if (== $answers ()) False
       (if (== (car-atom $answers) True) True (flow:has-true (cdr-atom $answers)))))

(: flow:until-quiet (-> Atom %Undefined%))
(= (flow:until-quiet $step) (flow:until-quiet-from $step 0))
(: flow:until-quiet-from (-> Atom %Undefined% %Undefined%))
(= (flow:until-quiet-from $step $rounds)
   (if (== (collapse (eval $step)) ())
       $rounds
       (flow:until-quiet-from $step (+ $rounds 1))))
