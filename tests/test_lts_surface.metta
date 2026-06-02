!(import! &self lts)

; Positive example: contract-satisfying frontier relation.
(= (mock:step a) (superpose (b c)))
(= (mock:step b) d)
(= (mock:step c) e)
(= (mock:step d) (superpose ()))
(= (mock:step e) (superpose ()))

!(assertEqualToResult
  (collapse (lts:transitions mock:step a))
  ((b c)))

!(assertEqual
  (collapse (lts:transitions mock:step a))
  (collapse (mock:step a)))

!(assertEqualToResult
  (lts:is-normal-form mock:step a)
  (False))

!(assertEqualToResult
  (lts:is-normal-form mock:step d)
  (True))

!(assertEqualToResult
  (lts:is-can-step mock:step a)
  (True))

!(assertEqualToResult
  (lts:is-can-step mock:step d)
  (False))

!(assertEqual
  (collapse (lts:transitions mock:step d))
  (collapse (mock:step d)))

!(assertEqualToResult
  (collapse (lts:trace-2 mock:step a))
  (((b d) (c e))))

!(assertEqualToResult
  (collapse
    (let $q (lts:transitions mock:step a)
      (lts:transitions mock:step $q)))
  ((d e)))

; Negative example: if a step relation leaves quiescent cases unstated,
; lts:is-normal-form cannot detect quiescence correctly.
(= (bad:step a) b)

!(assertEqualToResult
  (lts:is-normal-form bad:step z)
  (False))

!(assertEqualToResult
  (lts:is-can-step bad:step z)
  (True))

!(assertEqual
  (lts:transitions bad:step z)
  (bad:step z))

; Error example: a step relation that reports Error should propagate that
; contract through the generic lts:* helpers instead of looking quiescent.
(= (err:step x) (Error bad msg))

; Error example at the second hop: trace-2 must also propagate Error unchanged
; when the first hop succeeds but the second hop fails.
(= (hoperr:step a) b)
(= (hoperr:step b) (Error hop msg))

!(assertEqualToResult
  (lts:transitions err:step x)
  ((Error bad msg)))

!(assertEqual
  (lts:transitions err:step x)
  (err:step x))

!(assertEqualToResult
  (lts:trace-2 err:step x)
  ((Error bad msg)))

!(assertEqualToResult
  (lts:trace-2 hoperr:step a)
  ((Error hop msg)))

!(assertEqualToResult
  (lts:is-normal-form err:step x)
  ((Error bad msg)))

!(assertEqualToResult
  (lts:is-can-step err:step x)
  ((Error bad msg)))

!(assertEqualToResult
  (lts:transitions)
  ((Error (lts:transitions) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:trace-2)
  ((Error (lts:trace-2) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:is-normal-form)
  ((Error (lts:is-normal-form) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:is-can-step)
  ((Error (lts:is-can-step) IncorrectNumberOfArguments)))
