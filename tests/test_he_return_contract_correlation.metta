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
; The first complete argument-type path owns the result contract.  A later
; type of the same argument cannot rescue a result rejected by that path.
(: dependent-first-admitted-contract (-> $t $t))
(= (dependent-first-admitted-contract $x) 1)
!(assertEqualToResult
  (dependent-first-admitted-contract multiply-typed-argument)
  ((Error 1 (BadType String Number))))

(: dependent-multiple-contracts (-> $t $t))
(= (dependent-multiple-contracts $x) True)
!(assertEqualToResult
  (dependent-multiple-contracts multiply-typed-argument)
  ((Error True (BadType String Bool))))

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

; A polymorphic contract can admit an ordinary result.
(: overlap-polymorphic-first (-> $t Number))
(: overlap-polymorphic-first (-> Number String))
(= (overlap-polymorphic-first $x) 1)
!(assertEqual (overlap-polymorphic-first 1) 1)

; An explicit result demand participates in applicability before invocation.
; This selects the String contract independently of the type-query ordering.
(: overlap-string-demand (-> Number String))
(: overlap-string-demand (-> $t Number))
(= (overlap-string-demand $x) 1)
!(assertEqualToResult
  (metta (overlap-string-demand 1) String &self)
  ((Error 1 (BadType String Number))))

(: expression-result (-> Number Expression))
(: expression-result (-> String String))
(= (expression-result 1) "dynamic")
!(assertEqual (expression-result 1) "dynamic")

; A constructor's callable signature constrains its result before reduction.
; A declaration on the compound term alone does not enforce that demand.
(: boxed (-> Number String))
(: (boxed 1) String)
(: constructor-result (-> Number Number))
(: constructor-result (-> String String))
(= (constructor-result 1) (boxed 1))
!(assertEqualToResult
  (constructor-result 1)
  ((Error (boxed 1) (BadType Number String))))
