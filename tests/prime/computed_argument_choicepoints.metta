; A computed constructor spine must expose the same overlapping-clause
; alternatives as its literal normal form.  Prime previously retained only
; the first alternative after refining the suspended tail.
(= (choice:list $n)
   (if (== $n 0)
       Nil
       (Cons $n (choice:list (- $n 1)))))

(= (choice:pick (Cons $x $xs)) $x)
(= (choice:pick (Cons $x (Cons $y $ys)))
   (choice:pick (Cons $y $ys)))

!(choice:pick (Cons 2 (Cons 1 Nil)))
!(choice:pick (choice:list 2))

; Unequal syntax below a shared constructor is not WHNF determinacy evidence:
; the generated single-result machine must decline and preserve the ordinary
; evaluator's exact answer.
(= (nested:select (Box A)) first)
(= (nested:select (Box B)) second)
!(nested:select (Box B))
