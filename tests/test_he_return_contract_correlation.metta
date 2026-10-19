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

; Raw invocation protocol restores the selected call after argument evaluation.
; Completed native values and syntax under Atom demand do not enter this rule.
(= (decline-call $x) NotReducible)
!(assertEqualToResult (decline-call 7) ((decline-call 7)))
!(assertEqualToResult (decline-call (+ 1 2)) ((decline-call 3)))
!(assertEqualToResult (metta (decline-call 7) %Undefined% &self) ((decline-call 7)))
(= (decline-outer $x) (decline-call $x))
!(assertEqualToResult (decline-outer 7) ((decline-call 7)))
(= (decline-head) decline-call)
!(assertEqualToResult ((decline-head) (+ 1 2)) ((decline-call 3)))

(= (decline-choice) 1)
(= (decline-choice) 2)
!(assertEqualToResult (decline-call (decline-choice)) ((decline-call 1) (decline-call 2)))
(= (decline-duplicate $x) NotReducible)
(= (decline-duplicate $x) NotReducible)
!(assertEqualToResult (decline-duplicate 7) ((decline-duplicate 7) (decline-duplicate 7)))
(= (decline-mixed $x) NotReducible)
(= (decline-mixed $x) (ok $x))
!(assertEqualToResult (decline-mixed (+ 1 2)) ((decline-mixed 3) (ok 3)))

(= (decline-from-slot $x) $x)
!(assertEqualToResult (decline-from-slot NotReducible) ((decline-from-slot NotReducible)))
(: decline-number (-> Number Number))
(= (decline-number $x) NotReducible)
!(assertEqualToResult (decline-number (+ 1 2)) ((decline-number 3)))
(: hold-decline (-> Atom Atom))
(= (hold-decline $x) $x)
!(assertEqualToResult (hold-decline (decline-call (+ 1 2))) ((decline-call (+ 1 2))))
!(assertEqualToResult (metta (decline-call (+ 1 2)) Atom &self) ((decline-call (+ 1 2))))

; The raw protocol remains visible inside an explicit minimal program.
!(assertEqualToResult
  (function (chain (eval (decline-call 7)) $r (return (pair $r raw))))
  ((pair NotReducible raw)))
(= (decline-function $x) (function (return NotReducible)))
!(assertEqualToResult (decline-function (+ 1 2)) ((decline-function 3)))
(= (decline-binding $x)
  (function (chain (unify $x A ok bad) $r (return NotReducible))))
!(assertEqualToResult (decline-binding $caller) ((decline-binding A)))
(= (decline-constrained A) NotReducible)
!(assertEqualToResult
  (let $r (decline-constrained $caller) (pair $r $caller))
  ((pair (decline-constrained A) A)))

; A restored call is published without evaluating its arguments again.
!(bind! &decline-count (new-state 0))
(= (decline-effect)
  (let $u (change-state! &decline-count (+ (get-state &decline-count) 1)) (+ 1 2)))
!(assertEqualToResult (decline-call (decline-effect)) ((decline-call 3)))
!(assertEqual (get-state &decline-count) 1)

; A sentinel nested in completed data is not a raw invocation refusal.
(= (decline-data $x) (pair NotReducible $x))
!(assertEqualToResult (decline-data 7) ((pair NotReducible 7)))

; Case consumes the value and bindings of each source occurrence together.
(= (case-bind-a A) (case-value))
!(assertEqualToResult
  (case (case-bind-a $x) (((case-value) (pair $x yes))))
  ((pair A yes)))
!(assertEqualToResult
  (case (decline-constrained $x) (((decline-constrained A) (pair $x yes))))
  ((pair A yes)))
(= (case-bind-error A) (Error source reason))
!(assertEqualToResult
  (case (case-bind-error $x) (((Error source reason) (pair $x yes))))
  ((pair A yes)))
(= (case-bind-b A) B)
!(assertEqualToResult
  (case (case-bind-b $x) (($x wrong) (B (pair $x right))))
  ((pair A right)))
(= (case-same $x $x) ok)
!(assertEqualToResult
  (let $r (case-same $a $b)
    (case (case-same $b A) ((ok (pair $a $b)))))
  ((pair A A)))
(= (case-bind-duplicate A) (case-value))
(= (case-bind-duplicate A) (case-value))
!(assertEqualToResult
  (case (case-bind-duplicate $x) (((case-value) (pair $x yes))))
  ((pair A yes) (pair A yes)))
(= (case-bind-empty A) Empty)
!(assertEqualToResult
  (case (case-bind-empty $x) ((Empty (pair $x empty))))
  ((pair $x empty)))
