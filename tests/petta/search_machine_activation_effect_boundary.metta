(= (effectful-countdown $n)
   (if (== $n 0)
       true
       (let $_write (add-atom &self (countdown-step $n))
            (effectful-countdown (- $n 1)))))

!(effectful-countdown 3)
!(test (length
         (collapse
           (match &self (countdown-step $n) $n)))
       3)
!(test (match &self (countdown-step 3) true) true)
!(test (match &self (countdown-step 2) true) true)
!(test (match &self (countdown-step 1) true) true)

; A relation whose root is an ordered binding Region/Hole sequence may retain
; its source view across the sequence, but every effectful producer remains an
; explicit Hole.  The transient atom is the negative order canary.
(= (effectful-binding-sequence $value)
   (let* (($_add (add-atom &self (binding-effect-transient $value)))
          ($_remove (remove-atom &self (binding-effect-transient $value)))
          ($_publish (add-atom &self (binding-effect-present $value))))
      $value))

!(effectful-binding-sequence alpha)
!(test (match &self (binding-effect-present alpha) true) true)
!(test (collapse
         (match &self (binding-effect-transient alpha) $value))
       ())

; Zero discards its result demand, not an effect already performed by its
; producer. An extra argument also keeps the ordinary arity interpretation.
(= (effect-before-zero $value)
   (let $_write (add-atom &self (before-zero $value)) (empty)))
!(collapse (effect-before-zero observed))
!(collapse (match &self (before-zero observed) true))
(= (zero-with-extra-argument $value) (empty $value))
!(collapse (zero-with-extra-argument 7))
