; Populate the live program space with unrelated facts, then ask for one
; declared type.  Native type information is indexed by subject, so the query
; examines its small annotation bucket rather than the complete mutable space.

(: add-type-space-noise (-[det]-> Number Atom))
(= (add-type-space-noise $n)
   (if (== $n 0)
       Done
       (let $unit (add-atom &self (type-space-noise $n))
         (add-type-space-noise (- $n 1)))))

!(add-type-space-noise 5000)

(: type-space-target TargetType)

!(get-type type-space-target)
