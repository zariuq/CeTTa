; Why it matters: an agent that holds its own program as a value can inspect,
; describe and copy itself, and can hand a modified copy to its successor;
; this is the substrate for self-auditing and self-improving systems.
;
; Kleene's second recursion theorem at run time: (program n best) builds a
; program that receives its own code as an argument.  Each generation reads an
; observation, keeps the best value seen so far, describes its own code, and
; emits the code of its successor with the new state written into it.  The
; loop checks that every generation received exactly the code that was run.
; Code substitution uses an equation-level `lift let`, which is first-order
; and not capture-avoiding; a native, hygienic form is planned.  Here the
; slot x never occurs under a binder and the inserted code has no free name
; a binder could capture, which is what keeps the substitution exact.

; ---- substitution into code, one level, sealed at nested quotations
(: replace (-> (Data Atom) (Data Atom) (Data Atom) (Data Atom)))
(= (replace $s $v $t)
   (if (noreduce-eq $t $s)
       $v
       (if (== (get-metatype $t) Expression)
           (replace-children $s $v $t)
           $t)))
(: replace-children (-> (Data Atom) (Data Atom) (Data Atom) (Data Atom)))
(= (replace-children $s $v $t)
   (if (noreduce-eq $t ())
       ()
       (let ($h $rest) (decons-atom $t)
         (if (noreduce-eq $h quote)
             $t
             (let $h2 (replace $s $v $h)
               (let $rest2 (replace-children $s $v $rest)
                 (cons-atom $h2 $rest2)))))))
(= (lift let $s (quote $a) (quote $c))
   (let $r (replace $s $a $c) (quote $r)))

; diag(@c) = @(c[x := @c]): a program that mentions the slot x gets its own
; code in that place.
(= (diag $c) (lift let x @$c $c))

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
(= (program $gen $best) (diag @((lam s (agent $gen $best s)) (diag x))))

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
