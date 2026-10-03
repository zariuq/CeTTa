; One collection contains every result with its saved environment. The
; printer exposes a structural view of the otherwise opaque grounded value.
(= (multi) a)
(= (multi) b)
!(collapse-bind (eval (multi)))

; Empty branches contribute no occurrences to the saved answer bag.
!(collapse-bind
   (metta (let $x (superpose (a b)) (if (== $x a) (empty) $x))
      %Undefined% &self))

; Independent snapshots of the same environment compare as values.
!(assertEqual (collapse-bind 1) (collapse-bind 1))

; Fresh type-signature aliases are private, not part of a saved answer.
!(assertEqual
   (collapse-bind (metta (== b a) %Undefined% &self))
   (collapse-bind (metta (== b a) %Undefined% &self)))

; A closed invocation that returns no alternatives still runs exactly once.
!(bind! &collapse-effects (new-space))
(= (zero-effect)
   (function
      (chain (eval (add-atom &collapse-effects hit)) $unit (return Empty))))
!(assertEqualToResult (zero-effect) ())
!(assertEqual (collapse (match &collapse-effects hit hit)) (hit))
