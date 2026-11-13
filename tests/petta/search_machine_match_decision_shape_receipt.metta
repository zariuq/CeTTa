; A structural-verification receipt is reusable only for the exact query and
; Space revision under which the match-decision selector produced it.

; Literal selection.
(= (receipt-literal red) red-result)
(= (receipt-literal blue) blue-result)
(= (receipt-literal green) green-result)

; Deep structural selection and binding.
(= (receipt-deep (node red $x)) (red-value $x))
(= (receipt-deep (node blue $x)) (blue-value $x))
(= (receipt-deep (leaf $x)) (leaf-value $x))

; Equal authored occurrences retain multiplicity.
(= (receipt-duplicate token) same)
(= (receipt-duplicate token) same)

; Overlapping patterns retain both compatible occurrences.
(= (receipt-overlap (pair left $x)) (left-value $x))
(= (receipt-overlap (pair $x right)) (right-value $x))

; Simultaneously applicable alternatives each answer.
(= (receipt-ordered token) first)
(= (receipt-ordered token) second)

; The first alternative stages a Space mutation and then fails.  Collection
; keeps that intent outside the sibling's observation world, so the retained
; second alternative may reuse its receipt.  The mutation becomes visible at
; the collection boundary, after sibling selection is complete.
(= (receipt-added marker) old)
(= (receipt-mutating token)
   (let $_ (add-atom &self (= (receipt-added marker) added))
     (empty)))
(= (receipt-mutating token) survived)

(= (receipt-exercise)
   (let $literal (collapse (receipt-literal blue))
     (let $deep (collapse (receipt-deep (node blue payload)))
       (let $duplicate (collapse (receipt-duplicate token))
         (let $overlap (sort-atom (collapse (receipt-overlap (pair left right))))
           (let $ordered (sort-atom (collapse (receipt-ordered token)))
             (let $mutating (collapse (receipt-mutating token))
               (let $added (sort-atom (collapse (receipt-added marker)))
                 (receipt-result $literal $deep $duplicate $overlap
                   $ordered $mutating $added)))))))))

!(receipt-exercise)

; A wildcard contributes no rigid shape test. Its binding and repeated-variable
; constraints still belong to the authoritative match, including duplicates.
(= (receipt-wildcard (pair $x $x) $unused) same)
(= (receipt-wildcard (pair $x $y) $unused) any)
(= (receipt-wildcard (pair $x $y) $unused) any)
!(sort-atom (collapse (receipt-wildcard (pair a a) ignored)))
!(collapse (receipt-wildcard (pair a b) ignored))
!(sort-atom (collapse (receipt-wildcard (pair $open a) $ignored)))
!(collapse (receipt-wildcard (other a a) ignored))
