; Runtime dispatch of captured authored names in a frozen program.
(= (bound-call:increment $x) (+ $x 1))
(= (bound-call:decrement $x) (- $x 1))

!(BoundCallControl captured-symbol (== (let $f bound-call:increment ($f 7)) 8))
!(BoundCallControl nested-binding
  (== (let $f bound-call:increment (let $g bound-call:decrement ($g ($f 7)))) 7))
!(BoundCallControl strict-argument
  (== (let $f bound-call:increment ($f (bound-call:increment 7))) 9))
!(BoundCallControl alternatives
  (== (collapse (let $f bound-call:increment ($f (superpose (1 2))))) (quote (2 3))))
!(BoundCallControl repeated-occurrences
  (== (collapse (let $f bound-call:increment ($f (superpose (1 1))))) (quote (2 2))))
!(BoundCallControl unknown-head
  (== (let $f bound-call:unknown ($f 7)) (quote (bound-call:unknown 7))))
!(BoundCallControl unknown-strict-fields
  (== (let $f bound-call:unknown ($f (bound-call:increment 7))) (quote (bound-call:unknown 8))))
!(BoundCallControl quotation
  (== (let $f bound-call:increment (quote ($f 7))) (quote (bound-call:increment 7))))
