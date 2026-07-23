; Prime control-branch regressions.  These distinguish ordinary branch
; continuation, unary and binary recursive demand, and case dispatch while
; exercising delayed list construction inside an equation body.

(= (d:append () $ys) $ys)
(= (d:append $xs $ys)
   (let ($h $t) (decons-atom $xs)
     (let $rest (eval (d:append $t $ys))
       (cons-atom $h $rest))))
(= (d:consume ()) Done)
(= (d:consume $items)
   (let ($h $t) (decons-atom $items) (d:consume $t)))
(= (d:consume2 () $d) Done2)
(= (d:consume2 $items $d)
   (let ($h $t) (decons-atom $items) (d:consume2 $t $d)))
(= (d:mseq () $right) (== $right ()))
(= (d:mseq $left $right)
   (let ($h $t) (decons-atom $left)
     (case (d:remove-one $h $right)
       (((ROK $rem) (d:mseq $t $rem))
        ($_ False)))))
(= (d:remove-one $item ()) RM)
(= (d:remove-one $item $items)
   (let ($h $t) (decons-atom $items)
     (if (=alpha $item $h)
         (ROK $t)
         (case (d:remove-one $item $t)
           (((ROK $rest) (ROK (cons-atom $h $rest)))
            ($_ RM))))))
(= (d:i1) (if True (d:mseq (d:append ((Pos (q a))) ()) ((Pos (q a)))) False))

; W1: an if-branch inside an equation body whose branch needs a delayed
; nested call.  This is the control shape used by ATP check-polarity.
!(d:i1)

; W2: unary and binary recursive functions both demand their first argument
; inside a top-level if branch.
!(if True (d:consume ((A) (B))) False)
!(if True (d:consume2 ((A) (B)) dummy) False)

; W3: the corresponding case/direct controls remain equivalent.
!(d:mseq (d:append ((Pos (q a))) ()) ((Pos (q a))))
!(case (id X) ((X (d:consume ((A))))))
