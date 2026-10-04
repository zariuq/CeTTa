; Type demand belongs to the caller; a raw call does not interpret its result.
(: rb:atom (-> Atom Atom))
(= (rb:atom $value) $value)
(: rb:undefined (-> Atom %Undefined%))
(= (rb:undefined $value) $value)
(: rb:expression (-> Atom Expression))
(= (rb:expression $value) $value)
(: rb:many (-> Atom Atom))
(= (rb:many $value) $value)
(= (rb:many $value) (+ 1 2))

!(get-type eval)
!(get-type function)
!(get-type chain)
!(get-type superpose)
!(get-type evalc)
!(get-type cons-atom)
!(get-type decons-atom)
!(get-type unify)
!(get-type metta)
!(get-type context-space)
!(get-type collapse-bind)
!(get-type superpose-bind)

!(rb:atom (+ 1 2))
!(rb:many (+ 1 2))
!(rb:undefined (+ 1 2))
!(rb:expression (+ 1 2))
!(rb:atom (println! HELD-USER-PAYLOAD))
!(metta (+ 1 2) Atom &self)
!(metta (+ 1 2) Expression &self)
!(metta (+ 1 2) Number &self)
!(metta (+ 1 2) %Undefined% &self)
!(metta (println! HELD-METTA-PAYLOAD) Atom &self)

!(eval (+ 1 2))
!(eval (rb:undefined (+ 1 2)))
!(eval (superpose ((+ 1 2))))
!(eval (eval (superpose ((+ 1 2)))))
!(evalc (+ 1 2) &self)
!(evalc (rb:undefined (+ 1 2)) &self)
!(evalc (superpose ((+ 1 2))) &self)
!(superpose ((+ 1 2)))
!(chain (eval (superpose ((+ 1 2)))) $member $member)
!(function (return (+ 1 2)))
!(function
   (chain (eval (superpose ((+ 1 2)))) $member
      (return $member)))
!(function
   (chain (eval (superpose ((+ 1 2)))) $member
      (chain (eval $member) $answer
         (return $answer))))
!(let $member (eval (superpose ((+ 1 2)))) $member)

; Raw acquisition preserves occurrence multiplicity and caller bindings.
!(function
   (chain (eval (superpose (a a b))) $member
      (return $member)))
!(let $outer 7
   (function
      (chain (eval (superpose (($outer 2)))) $pair
         (return $pair))))
!(function
   (chain (eval (superpose ((println! HELD-MEMBER-PAYLOAD)))) $member
      (return $member)))

; Zero alternatives must not activate a dependent body or a return payload.
!(eval (superpose ()))
!(function
   (chain (eval (superpose ())) $member
      (return $member)))
; A closed return payload is retained by the reference, but remains data.
!(function
   (chain (eval (superpose ())) $member
      (return (println! EMPTY-RETURN-MUST-NOT-RUN))))
!(let $member
   (function
      (chain (eval (superpose ())) $result (return $result)))
   (println! EMPTY-BODY-MUST-NOT-RUN))

; Space acquisition uses the same raw/result boundary as member acquisition.
!(bind! &rb:bag (new-space))
!(add-atom &rb:bag (+ 1 2))
!(add-atom &rb:bag (+ 1 2))
!(match &rb:bag $item $item)
!(function
   (chain (eval (match &rb:bag $item $item)) $member
      (return $member)))
!(function
   (chain (evalc (match &rb:bag $item $item) &self) $member
      (return $member)))
!(function
   (chain (eval (match &rb:bag absent $item)) $member
      (return $member)))
!(bind! &rb:empty (new-space))
!(function
   (chain (eval (match &rb:empty $item $item)) $member
      (return $member)))

; Context selection changes equation lookup, not interpretation of the result.
!(bind! &rb:rules (new-space))
!(add-atom &rb:rules (= (rb:local) (+ 4 5)))
!(evalc (rb:local) &rb:rules)
!(function
   (chain (evalc (rb:local) &rb:rules) $member
      (return $member)))
!(metta (rb:local) %Undefined% &rb:rules)
!(metta (rb:local) Atom &rb:rules)

; A raw equation call performs only its own rewrite.
(= (rb:first) (rb:second))
(= (rb:second) (+ 1 2))
!(eval (rb:first))
!(evalc (rb:first) &self)
!(function
   (chain (eval (rb:first)) $first
      (chain (eval $first) $second (return $second))))
!(rb:first)

; A produced eval remains syntax; a produced function supplies a delimiter.
(= (rb:produced-eval) (eval (+ 1 2)))
(= (rb:produced-function) (function (return (+ 1 2))))
!(eval (rb:produced-eval))
!(eval (rb:produced-function))

; Raw native results retain new bindings and their caller's lexical bindings.
!(function
   (chain (eval (match &rb:bag (+ $x $y) ($y $x))) $pair
      (return $pair)))
!(let $x 7
   (let $y 9
      (function
         (chain (eval (match &rb:bag $item ($x $y $item))) $answer
            (return $answer)))))
(= (rb:from-space $x)
   (function
      (chain (eval (match &rb:bag $item $item)) $answer
         (return ($x $answer)))))
!(eval (rb:from-space 7))

; Minimal unification selects raw syntax; a function may resume that syntax.
!(function
   (chain (unify A A (eval (+ 1 2)) (eval (+ 3 4))) $picked
      (return $picked)))
!(function
   (chain (unify A B (eval (+ 1 2)) (eval (+ 3 4))) $picked
      (return $picked)))
!(function
   (unify A A
      (chain (eval (+ 1 2)) $value (return $value))
      (return wrong)))
!(function
   (unify A B
      (return wrong)
      (chain (eval (+ 3 4)) $value (return $value))))

; Explicit interpretation and current-space acquisition are valid instructions.
!(function
   (chain (metta (+ 1 2) Atom &self) $held (return $held)))
!(function
   (chain (metta (+ 1 2) %Undefined% &self) $answer (return $answer)))
!(function
   (chain (metta (+ 1 2) Atom &self) $held
      (chain (eval $held) $answer (return $answer))))
!(function
   (chain (context-space) $space
      (chain (metta (+ 1 2) %Undefined% $space) $answer
         (return $answer))))
!(function
   (chain (context-space) $space
      (chain (evalc (rb:first) $space) $answer (return $answer))))

; Restoration merges saved and incoming bindings instead of overwriting either.
!(chain (collapse-bind (unify $x A token Empty)) $saved
   (chain (superpose-bind $saved) $answer (choice $answer $x)))
!(chain (collapse-bind (unify $x A token Empty)) $saved
   (unify $y B
      (chain (superpose-bind $saved) $answer (choice $answer $x $y))
      Empty))
!(chain (collapse-bind (unify $x A token Empty)) $saved
   (unify $x B
      (chain (superpose-bind $saved) $answer (choice $answer $x))
      Empty))
!(chain (collapse-bind (unify $x (pair $y) token Empty)) $saved
   (unify $x (pair B)
      (chain (superpose-bind $saved) $answer (choice $answer $x $y))
      Empty))

; A returned function executes its delimiter before the caller's result demand.
(= (rb:plain-function $x)
   (function (chain (+ $x 10) $value (return $value))))
(: rb:typed-function (-> Number Atom))
(= (rb:typed-function $x)
   (function (chain (+ $x 10) $value (return $value))))
(: rb:undefined-function (-> Number %Undefined%))
(= (rb:undefined-function $x)
   (function (chain (+ $x 10) $value (return $value))))
(: rb:two-functions (-> Number Atom))
(= (rb:two-functions $x) (function (return (+ $x 10))))
(= (rb:two-functions $x) (function (return (+ $x 20))))
!(rb:plain-function 5)
!(rb:typed-function 5)
!(rb:undefined-function 5)
!(rb:two-functions 5)

; Holding an unevaluated source function is distinct from invoking its producer.
!(metta (function (return (+ 5 10))) Atom &self)
!(metta (function (return (+ 5 10))) %Undefined% &self)
!(eval (rb:plain-function 5))
!(eval (rb:typed-function 5))

; Intrinsic observers execute as raw calls, including inside a delimiter.
!(eval (get-type 3))
!(eval (get-metatype (+ 1 2)))
!(function (chain (eval (get-type 3)) $type (return $type)))
!(function (chain (eval (get-metatype (+ 1 2))) $type (return $type)))

; An ordinary helper call is source syntax until a minimal eval invokes it.
!(function (chain (return-on-error ok later) $value (return $value)))
!(function (chain (eval (return-on-error ok later)) $value (return $value)))

; Assertions compare computation results; holding a sentinel is distinct.
!(assertEqual (get-doc &self NoSuchAtom) Empty)
!(assertEqual Empty (superpose ()))
!(assertEqualMsg Empty (superpose ()) "empty")
!(== (collapse Empty) (metta (Empty) Atom &self))

; Full callback interpretation respects the callback's own return demand.
!(function
   (chain (eval (map-atom (1 2) $item
      (metta (rb:atom (+ $item 10)) %Undefined% &self))) $mapped
      (return $mapped)))
!(function
   (chain (eval (map-atom (1 2) $item
      (metta (rb:undefined (+ $item 10)) %Undefined% &self))) $mapped
      (return $mapped)))

; Identical callback heads can denote different equations in different spaces.
(= (rb:callback-view $item) (+ $item 1))
!(add-atom &rb:rules (= (rb:callback-view $item) (+ $item 100)))
!(function
   (chain (eval (map-atom (1 2) $item
      (metta (rb:callback-view $item) %Undefined% &rb:rules))) $mapped
      (return $mapped)))

; The authored map visits the tail first but restores input result order.
; Effects occur once per occurrence, including duplicate inputs.
!(function
   (chain (eval (map-atom (1 1 2) $item
      (metta
         (let $_ (add-atom &self (rb:callback-seen $item)) (+ $item 100))
         %Undefined% &self))) $mapped
      (return $mapped)))
!(collapse (match &self (rb:callback-seen $item) $item))

; Public collection applies its Expression result demand before observation.
; The return delimiter holds the projected field instead of re-evaluating it.
!(let (($member $bindings))
   (collapse-bind (eval (superpose ((+ 1 2)))))
   (function (return $member)))

; The minimal collection instead records raw invocation answers.
!(function
   (chain (collapse-bind (eval (superpose ((+ 1 2))))) $pairs
      (unify (($member $bindings)) $pairs
         (return $member)
         (return unexpected))))

; Public intrinsic applicability rejects incompatible codomains before effects.
!(assertEqualToResult
   (metta (context-space) Number &self)
   ((Error (context-space) (BadType Number SpaceType))))
!(assertEqualToResult
   (metta (collapse-bind (eval (println! INVALID-COLLAPSE-MUST-NOT-RUN)))
      Number &self)
   ((Error (collapse-bind (eval (println! INVALID-COLLAPSE-MUST-NOT-RUN)))
      (BadType Number Expression))))
!(assertEqualToResult
   (cons-atom held 3)
   ((Error (cons-atom held 3) (BadArgType 2 Expression Number))))
!(assertEqualToResult
   (decons-atom 3)
   ((Error (decons-atom 3) (BadArgType 1 Expression Number))))

; Accepted context and collection requests can be observed without space printing.
!(function
   (chain (metta (context-space) SpaceType &self) $space
      (chain (eval (== $space &self)) $same (return $same))))
!(let (($member $bindings))
   (metta (collapse-bind (eval (superpose ((+ 1 2))))) %Undefined% &self)
   (function (return $member)))
!(metta (collapse-bind (eval (superpose ((+ 1 2))))) Atom &self)
!(metta (collapse-bind (eval (superpose ((+ 1 2))))) Expression &self)

; Public metta checks its Type argument; the minimal instruction uses it directly.
; Projecting the error reason avoids the diagnostic's opaque context-space field.
!(function
   (chain (metta (metta (+ 1 2) 3 &self) %Undefined% &self) $error
      (unify (Error $subject $reason) $error
         (return $reason) (return unexpected))))
!(function
   (chain (metta (+ 1 2) 3 &self) $error
      (unify (Error $subject $reason) $error
         (return $reason) (return unexpected))))

; Concrete intrinsic codomains refine the caller's requested type variable.
!(function
   (chain (metta (context-space) $T &self) $value (return $T)))

; Atom codomains remain wildcard-compatible, not a concrete rejection request.
!(metta (eval (+ 1 2)) Number &self)
!(metta (function (return (+ 1 2))) Number &self)

; Ordinary typed calls publish requested-type refinements with caller bindings.
(: rb:typed-id (-> Number Number))
(= (rb:typed-id $x) $x)
!(function
   (chain (metta (rb:typed-id 7) $T &self) $value
      (return ($value $T))))
!(let $outer caller-binding
   (function
      (chain (metta (rb:typed-id 7) $T &self) $value
         (return ($outer $value $T)))))

; Applicability commits to the first accepted type, without duplicating answers.
(: rb:A Type)
(: rb:B Type)
(: rb:ambiguous rb:A)
(: rb:ambiguous rb:B)
(: rb:poly-id (-> $t $t))
(= (rb:poly-id $x) $x)
!(function
   (chain (metta (rb:poly-id rb:ambiguous) $T &self) $value
      (return ($value $T))))

; Fresh signature variables cannot capture or bind caller variables of that name.
!(let $t caller-type
   (function
      (chain (metta (rb:poly-id rb:ambiguous) $T &self) $value
         (return ($value $T $t)))))
!(function
   (chain (metta (rb:poly-id rb:ambiguous) $T &self) $first
      (chain (metta (rb:poly-id 7) $U &self) $second
         (return ($first $T $second $U)))))
!(function
   (chain (metta (rb:poly-id rb:ambiguous) $T &self) $value
      (chain (eval (get-type $t)) $unbound-type
         (return ($value $T $unbound-type)))))

; Public superposition checks its Expression domain before invocation.
!(assertEqualToResult
   (superpose 1)
   ((Error (superpose 1) (BadArgType 1 Expression Number))))

; Conflicting restored bindings reject before a public continuation's effect.
!(chain (collapse-bind (unify $x A token Empty)) $saved
   (unify $x B
      (chain (superpose-bind $saved) $answer
         (add-atom &self rb:conflict-effect-public))
      Empty))
!(collapse (match &self rb:conflict-effect-public observed))

; An explicitly embedded metta must receive the same incoming constraints.
; A late conflict check could hide the answer while leaving this effect behind.
!(function
   (chain (collapse-bind (unify $x A token Empty)) $saved
      (unify $x B
         (chain
            (metta
               (chain (superpose-bind $saved) $answer
                  (add-atom &self rb:conflict-effect-metta))
               %Undefined% &self)
            $effect
            (return $effect))
         Empty)))
!(collapse (match &self rb:conflict-effect-metta observed))

; Public superposition interprets returned payloads under its result demand.
; A minimal invocation instead delivers the completed payload as raw data.
!(superpose ((function (return (Box (+ 1 2))))))
!(eval (superpose ((function (return (Box (+ 1 2)))))))
!(assertEqualToResult
   (superpose ((function (return (Box (Error held data))))))
   ((Error held data)))
!(assertEqualToResult
   (eval (superpose ((function (return (Box (Error held data)))))))
   ((Box (Error held data))))

; The distinction also governs whether an effectful payload is activated.
(= (rb:superpose-public-payload)
   (add-atom &self rb:superpose-public-effect))
(= (rb:superpose-raw-payload)
   (add-atom &self rb:superpose-raw-effect))
!(superpose ((function (return (rb:superpose-public-payload)))))
!(collapse (match &self rb:superpose-public-effect observed))
!(eval (superpose ((function (return (rb:superpose-raw-payload))))))
!(collapse (match &self rb:superpose-raw-effect observed))

; An evaluated callable head retains the caller's incoming constraints.
; A conflicting captured answer cannot activate the head maker's effect.
!(bind! &rb:callable-effects (new-space))
(: rb:callable-id (-> Atom Atom))
(= (rb:callable-id $item) $item)
(: rb:callable-maker (-> Atom (-> Atom Atom)))
(= (rb:callable-maker $saved)
   (function
      (chain (superpose-bind $saved) $answer
         (chain (eval (add-atom &rb:callable-effects forbidden)) $effect
            (return rb:callable-id)))))
!(function
   (chain (collapse-bind (unify $x A token Empty)) $saved
      (unify $x B
         (chain (metta ((rb:callable-maker $saved) data) %Undefined% &self)
            $value (return $value))
         Empty)))
!(collapse (match &rb:callable-effects $item $item))

; A failed metatype shortcut falls back to inferred-type applicability.
; An untyped symbol reaches the native domain check through Undefined.
!(function
   (chain (metta (cons-atom a not-an-expression) %Undefined% &self) $error
      (unify (Error $subject $reason) $error
         (return $reason) (return unexpected))))
!(function
   (chain (metta (decons-atom not-an-expression) %Undefined% &self) $error
      (unify (Error $subject $reason) $error
         (return $reason) (return unexpected))))

; A produced function acquires its own Atom return contract even when the
; enclosing public superposition has exactly one surviving alternative.
!(superpose ((function (return (function (return (+ 1 2)))))))
!(superpose
   ((function (return (function (return (+ 1 2)))))
    (function (return (function (return (+ 1 2)))))))

; Symbols are data; eval acquires precisely one rewrite result.
(= rb:constant 5)
(= rb:alias rb:constant)
(= rb:duplicate 7)
(= rb:duplicate 7)
(= rb:cycle-a rb:cycle-b)
(= rb:cycle-b rb:cycle-a)
! rb:constant
!(eval rb:constant)
!(rb:constant)
!(eval rb:alias)
!(chain (eval rb:alias) $next (eval $next))
!(eval rb:duplicate)
!(eval rb:cycle-a)
; A builtin spelling is still a data value, even without that extension.
! once
!(bind! &rb:symbols (new-space))
!(add-atom &rb:symbols once)
!(add-atom &rb:symbols once)
!(add-atom &rb:symbols plain)
!(get-atoms &rb:symbols)
!(match &rb:symbols $value $value)
!(collapse (match &rb:symbols $value $value))
; Profile filtering must retain declarations authored in an ordinary space.
!(add-atom &rb:symbols (: once Number))
!(get-type-space &rb:symbols once)
; Raw return acquires the effect; the enclosing eval activates it once.
(= rb:effect (add-atom &rb:symbols once))
!(chain (eval rb:effect) $effect (eval $effect))
!(collapse (match &rb:symbols once retained))

; Selecting a function type commits its demand even with a data annotation.
(: rb:mixed-held rb:Information)
(: rb:mixed-held (-> Atom Atom))
(= (rb:mixed-held $x) (Held $x))
!(rb:mixed-held (+ 1 2))
!(rb:mixed-held (println! MIXED-HELD-MUST-NOT-RUN))
(: rb:mixed-empty rb:Information)
(: rb:mixed-empty (-> Atom %Undefined%))
(= (rb:mixed-empty $x) Empty)
!(rb:mixed-empty (println! MIXED-EMPTY-MUST-NOT-RUN))
!(bind! &rb:mixed-effects (new-space))
(: rb:mixed-effect rb:Information)
(: rb:mixed-effect (-> Atom %Undefined%))
(= (rb:mixed-effect $x) (add-atom &rb:mixed-effects performed))
!(rb:mixed-effect (println! MIXED-EFFECT-ARG-MUST-NOT-RUN))
!(collapse (match &rb:mixed-effects performed observed))
