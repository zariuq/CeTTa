; An indirect iteration preserves values, multiplicity and effects when
; admitted as one prepared call or evaluated through ordinary continuations.
(= (indirect-iterate $f $n $state)
   (if (== $n 0) $state
       (indirect-iterate $f (- $n 1) ($f $state $n))))
(= (indirect-sum $state $n) (+ $state $n))
(= (indirect-tag ($tag $state) $n) ($tag (+ $state $n)))
(= (indirect-note $state $n)
   (let $_ (add-atom &iteration-notes (seen $n)) (+ $state $n)))
(= (indirect-choice $state $n) (+ $state 1))
(= (indirect-choice $state $n) (+ $state 2))
!(indirect-iterate indirect-sum 10000 0)
!(indirect-iterate indirect-tag 5000 (tag 0))
!(indirect-iterate indirect-note 3 0)
!(sort-atom (collapse (match &iteration-notes (seen $n) $n)))
!(sort-atom (collapse (indirect-iterate indirect-choice 2 0)))
!(indirect-iterate no-equation 1 0)
!(indirect-iterate indirect-note 0 42)
!(size-atom (collapse (match &iteration-notes (seen $n) $n)))
