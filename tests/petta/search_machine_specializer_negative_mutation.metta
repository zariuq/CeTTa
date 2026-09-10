(= (negative-mutation-h $x) (+ $x 10))
(Data negative-mutation-h)
(= (negative-mutation-g $x) (match &self (Data $x) (Payload $x)))
(= (negative-mutation-f $x) (negative-mutation-g $x))

; A failed caller specialization depends on the callee's current definition.
!(negative-mutation-f negative-mutation-h)
!(add-atom &self (= (negative-mutation-g $f) ($f 1)))
!(negative-mutation-f negative-mutation-h)

; Derived equations are observable through the ordinary space interface.
!(match &self (= (negative-mutation-f_Spec_[negative-mutation-h] $x) $rhs) $rhs)
