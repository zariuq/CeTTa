; Six unrelated equation families run in two Hyperpose episodes.  The
; observable contract is only the answer bag: authored duplicates remain
; duplicates, and scheduling contributes no order.

(= (revision-view-linear $x) 1)
(= (revision-view-repeated $x $x) 2)
(= (revision-view-rigid nested) 3)
(= (revision-view-choice key) 4)
(= (revision-view-choice key) 4)
(= (revision-view-wildcard marker) 5)
(= (revision-view-recursive $n)
   (if (== $n 0)
       6
       (revision-view-recursive (- $n 1))))

!(msort
   (collapse
     (hyperpose
       ((revision-view-linear value)
        (revision-view-repeated same same)
        (revision-view-rigid nested)
        (revision-view-choice key)
        (revision-view-wildcard marker)
        (revision-view-recursive 4)))))
!(msort
   (collapse
     (hyperpose
       ((revision-view-linear value)
        (revision-view-repeated same same)
        (revision-view-rigid nested)
        (revision-view-choice key)
        (revision-view-wildcard marker)
        (revision-view-recursive 4)))))
