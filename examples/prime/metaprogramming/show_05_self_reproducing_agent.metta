; Why it matters: an agent that holds its own program as a value can inspect,
; describe and copy itself, and can hand a modified copy to its successor;
; this is the substrate for self-auditing and self-improving systems.
;
; Kleene's second recursion theorem at run time: (program n best) builds a
; program that receives its own code as an argument.  Each generation reads an
; observation, keeps the best value seen so far, describes its own code, and
; emits the code of its successor with the new state written into it.  The
; loop checks that every generation received exactly the code that was run.
; Code substitution is the native `lift let`, lambda's capture-avoiding
; substitution, and (lift M) is the code of M's value.  A quotation is
; sealed, so a generation's state enters its code by lift.

; diag(@c) = @(c[x := @c]): a program that mentions the slot x gets its own
; code in that place.
(= (diag $c) (lift let x (lift $c) $c))

; ---- the environment and the agent
(= (observation 0) 3)
(= (observation 1) 7)
(= (observation 2) 5)
(= (observation 3) 9)
(= (larger $a $b) (if (> $a $b) $a $b))

(= (agent $gen $best $self)
   (let $seen (observation $gen)
     (let $kept (larger $best $seen)
       (let $me $self
         (let $next (+ $gen 1)
           (Generation $gen $seen $kept (repr $me) (program $next $kept) $me))))))

; The program of one generation, with its state written into its code.
(= (program $gen $best)
   (diag (lift let gen (lift $gen)
           (lift let best (lift $best)
             @((lam s (agent gen best s)) (diag x))))))

; ---- the loop: run a generation's code, check its self-knowledge, go on
(= (live $code $k)
   (if (> $k 0)
       (let $c $code
         (let (Generation $g $seen $kept $description $child $self) *$c
           (let $knows-itself (noreduce-eq $self $c)
             (let $rest (live $child (- $k 1))
               (cons-atom (Gen $g (saw $seen) (best $kept) (knows-itself $knows-itself)) $rest)))))
       ()))

; Generation 0's code.
!(program 0 0)
; What generation 0 says about itself, and the code it hands on: the same
; program with the new state written in.
!(let $c (program 0 0)
   (let (Generation $g $seen $kept $description $child $self) *$c $description))
!(let $c (program 0 0)
   (let (Generation $g $seen $kept $description $child $self) *$c $child))
; Four generations.
!(live (program 0 0) 4)
