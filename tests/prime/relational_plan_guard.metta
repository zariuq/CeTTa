; Values-only relational episodes must preserve Prime's demand, effect, fault,
; and rollback boundaries.  The Makefile runs this fixture both canonically
; and through the guarded Prime relation-plan probe.

!(bind! &plan-guard-space (new-space))
!(add-atom &plan-guard-space (value kept))

; A demanded bound name denotes its stored value.  An unbound name remains
; ordinary data; merely resembling a reference cannot manufacture a value.
!(bind! &plan-guard-value (Tagged payload))
(= (plan-guard-read-value $value)
   (case $value
     (((Tagged $item) (Resolved $item))
      ($_ (Unresolved $value)))))
!(plan-guard-read-value &plan-guard-value)
!(plan-guard-read-value &plan-guard-unbound)

; Search machinery may decide truth, but Prime owns the public Boolean datum.
(= (plan-guard-boolean-case $value $expected)
   (case $value
     (((Tagged $actual) (== $actual $expected))
      ($_ False))))
!(plan-guard-boolean-case (Tagged same) same)
!(plan-guard-boolean-case (Tagged same) different)

(= (plan-guard-ignore $unused)
   (match &plan-guard-space (value $value) $value))

; Neither an effect nor a pure loop in an unused argument may be forced.
!(plan-guard-ignore (println! SHOULD-NOT-PRINT))
(= (plan-guard-loop) (plan-guard-loop))
!(plan-guard-ignore (plan-guard-loop))

; A nested closed, total, type-pure argument may cross the eager machine
; boundary.  A faulting pure argument may not: the callee below ignores it,
; so canonical Prime must not turn that dormant fault into control.
(= (plan-guard-decrement $n) (- $n 1))
(= (plan-guard-nested-total $n)
   (plan-guard-ignore (- $n 1)))
!(plan-guard-decrement 3)
!(plan-guard-nested-total 4)
(= (plan-guard-nested-unused-fault $n)
   (plan-guard-ignore (/ $n 0)))
!(plan-guard-nested-unused-fault 1)

; A late unsafe dynamic target must discard earlier staged answers, then let
; canonical Prime execute the effect exactly once.
(= (plan-guard-branch $callable) safe)
(= (plan-guard-branch $callable) ($callable Marker))
!(plan-guard-branch println!)

; Inert dynamic data remains data, while a pure fault remains a fault.
(= (plan-guard-invoke $callable $value) ($callable $value))
!(plan-guard-invoke Unknown payload)
(= (plan-guard-fault)
   (let $value
        (match &plan-guard-space (value $found) $found)
        (/ 1 0)))
!(plan-guard-fault)
