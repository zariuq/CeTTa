; A call must check its result against the return contract instantiated by the
; same function type and argument-type substitution that admitted the call.

(: overload-bad (-> Number Number))
(: overload-bad (-> String String))
(= (overload-bad 1) "wrong")
!(assertEqualToResult
  (overload-bad 1)
  ((Error "wrong" (BadType Number String))))

(: overload-bad-reversed (-> String String))
(: overload-bad-reversed (-> Number Number))
(= (overload-bad-reversed 1) "wrong")
!(assertEqualToResult
  (overload-bad-reversed 1)
  ((Error "wrong" (BadType Number String))))

(: overload-bad-other-arity (-> Number Number))
(: overload-bad-other-arity (-> Number Number Number))
(= (overload-bad-other-arity 1) "wrong")
!(assertEqualToResult
  (overload-bad-other-arity 1)
  ((Error "wrong" (BadType Number String))))

(: multiply-typed-value String)
(: multiply-typed-value Bool)
(: multiply-typed-result (-> Number Number))
(: multiply-typed-result (-> String String))
(= (multiply-typed-result 1) multiply-typed-value)
!(assertEqualToResult
  (multiply-typed-result 1)
  ((Error multiply-typed-value (BadType Number String))
   (Error multiply-typed-value (BadType Number Bool))))

(: dependent-good (-> $t $t))
(: dependent-good (-> Number Number Number))
(= (dependent-good $x) $x)
!(assertEqual (dependent-good 7) 7)

(: dependent-bad (-> $t $t))
(= (dependent-bad $x) "wrong")
!(assertEqualToResult
  (dependent-bad 7)
  ((Error "wrong" (BadType Number String))))

(: dependent-overload-bad (-> $t $t))
(: dependent-overload-bad (-> Number Number Number))
(= (dependent-overload-bad $x) "wrong")
!(assertEqualToResult
  (dependent-overload-bad 7)
  ((Error "wrong" (BadType Number String))))

(: multiply-typed-argument String)
(: multiply-typed-argument Number)
(: dependent-any-surviving-contract (-> $t $t))
(= (dependent-any-surviving-contract $x) 1)
!(assertEqual
  (dependent-any-surviving-contract multiply-typed-argument)
  1)

(: dependent-multiple-contracts (-> $t $t))
(= (dependent-multiple-contracts $x) True)
!(assertEqualToResult
  (dependent-multiple-contracts multiply-typed-argument)
  ((Error True (BadType String Bool))
   (Error True (BadType Number Bool))))

(: error-passthrough (-> Number Number))
(: error-passthrough (-> String String))
(= (error-passthrough 1) (Error source reason))
!(assertEqualToResult
  (error-passthrough 1)
  ((Error source reason)))

(: empty-passthrough (-> Number Number))
(: empty-passthrough (-> String String))
(= (empty-passthrough 1) Empty)
!(assertEqualToResult (empty-passthrough 1) ())

(: atom-result (-> Number Atom))
(: atom-result (-> String String))
(= (atom-result 1) "dynamic")
!(assertEqual (atom-result 1) "dynamic")

(: undefined-result (-> Number %Undefined%))
(: undefined-result (-> String String))
(= (undefined-result 1) "dynamic")
!(assertEqual (undefined-result 1) "dynamic")

; When multiple contracts accept the same argument, the first applicable
; declaration owns the result contract.  A later overload cannot be used to
; reinterpret either success or failure.
(: overlap-order-accept (-> $t Number))
(: overlap-order-accept (-> Number String))
(= (overlap-order-accept $x) 1)
!(assertEqual (overlap-order-accept 1) 1)

(: overlap-order-reject (-> Number String))
(: overlap-order-reject (-> $t Number))
(= (overlap-order-reject $x) 1)
!(assertEqualToResult
  (overlap-order-reject 1)
  ((Error 1 (BadType String Number))))

(: expression-result (-> Number Expression))
(: expression-result (-> String String))
(= (expression-result 1) "dynamic")
!(assertEqual (expression-result 1) "dynamic")

(: (boxed 1) String)
(: constructor-result (-> Number Number))
(: constructor-result (-> String String))
(= (constructor-result 1) (boxed 1))
!(assertEqualToResult
  (constructor-result 1)
  ((Error (boxed 1) (BadType Number String))))
