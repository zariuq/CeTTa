!(import! &self lts)

; Positive example: contract-satisfying frontier relation.
(= (mock:step a) (superpose (b c)))
(= (mock:step b) d)
(= (mock:step c) e)
(= (mock:step d) (superpose ()))
(= (mock:step e) (superpose ()))
(= (mock:true $_) True)
(= (mock:false $_) False)
(= (mock:is-b b) True)
(= (mock:is-b $_) False)
(= (mock:not-b b) False)
(= (mock:not-b $_) True)

; Mixed frontier policy: a frontier containing both Error and ordinary
; successors is treated as an ordinary frontier rather than being promoted
; to Error. Only a pure singleton Error frontier is propagated.
(= (mixed:step x) (superpose ((Error mixed msg) ok)))

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
  (lts:box mock:step mock:false a)
  (lts:is-normal-form mock:step a))

!(assertEqual
  (lts:box mock:step mock:false d)
  (lts:is-normal-form mock:step d))

!(assertEqual
  (lts:diamond mock:step mock:true a)
  (lts:is-can-step mock:step a))

!(assertEqual
  (lts:diamond mock:step mock:true d)
  (lts:is-can-step mock:step d))

!(assertEqualToResult
  (lts:box mock:step mock:true a)
  (True))

!(assertEqualToResult
  (lts:diamond mock:step mock:false a)
  (False))

!(assertEqual
  (lts:box mock:step mock:is-b a)
  (not (lts:diamond mock:step mock:not-b a)))

!(assertEqualToResult
  (lts:box mixed:step mock:true x)
  (True))

!(assertEqualToResult
  (lts:diamond mixed:step mock:false x)
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

!(assertEqualToResult
  (lts:box)
  ((Error (lts:box) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:diamond)
  ((Error (lts:diamond) IncorrectNumberOfArguments)))
