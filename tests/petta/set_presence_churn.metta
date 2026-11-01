; A set keeps its rows through removals and adds; each add after a removal
; meets the presence bits the removal kept current.
!(bind! &s (new-space set))
(= (fill $n) (if (== $n 0) done (let $u (add-atom &s (r $n $X)) (fill (- $n 1)))))
!(fill 300)
(= (churn $n) (if (== $n 0) done (let $u (remove-atom &s (r $n $Y)) (let $v (add-atom &s (r $n $Z)) (churn (- $n 1))))))
!(churn 300)
!(size-atom (collapse (get-atoms &s)))
!(add-atom &s (g 1))
!(add-atom &s (g 1))
!(size-atom (collapse (get-atoms &s)))
!(remove-atom &s (g 1))
!(add-atom &s (g 1))
!(size-atom (collapse (get-atoms &s)))
