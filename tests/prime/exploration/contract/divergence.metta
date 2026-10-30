; Ignored divergence.  A parameter that is never used is never computed, so a
; diverging or failing argument in an unused position cannot change the
; answer.  Predicted by konstLoop_need, konstFail_need and konstPick_need of the
; call-time-choice core; eager evaluation would have no observation at all
; (konstLoop_value_diverges).  Held code is never run either, under any
; strategy (konstHeldLoop_value).

(= (konst $x $y) $x)
(= (loop) (loop))
(= (pick) A)
(= (pick) B)

; 1. An unused diverging argument.
!(konst A (loop))

; 2. An unused failing argument.
!(konst A (empty))

; 3. An unused choice does not multiply the answers.
!(konst A (pick))

; 4. A diverging argument held as code, or as data, is not run.
!(konst A @(loop))
(: hold (-> Atom Proc))
(= (hold $x) done)
!(hold (loop))

; 5. A field that is never demanded stays unevaluated.
(= (hd (Cons $a $b)) $a)
!(hd (Cons 1 (loop)))
