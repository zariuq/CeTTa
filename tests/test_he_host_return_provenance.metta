; Host result classification is a profile policy. HE compatibility uses the
; ordinary return continuation; the other HE profiles keep ready host values.
(: plant (-> Atom Atom))
(= (plant $x) (new-state $x))
(= (foo $x) (bar $x))
!(let $s (plant (foo 1)) (get-state $s))
!(let $s (plant (foo 1)) (metta (get-state $s) %Undefined% &self))
!(metta (get-state (plant (foo 1))) Atom &self)

; Raw refusal belongs to the post-argument invocation, including through metta.
!(let $s (new-state NotReducible)
   (case (get-state $s) (((get-state $s) restored) (NotReducible completed))))
!(let $s (new-state NotReducible)
   (case (metta (get-state $s) %Undefined% &self)
     (((get-state $s) restored) ((metta (get-state $s) %Undefined% &self) outer)
      (NotReducible completed))))

; The same read through aliases still denotes two occurrences.
!(let $s (plant (foo 2))
   (let $alias $s (collapse (superpose ((get-state $s) (get-state $alias))))))

; One-root minimal evaluation exposes a raw result, without interpreting foo.
!(bind! &raw-box (new-state (raw-foo 3)))
(= (raw-foo $x) (raw-bar $x))
!(function (chain (eval (get-state &raw-box)) $r (return (pair $r raw))))
!(let $s (new-state NotReducible)
   (function (chain (eval (get-state $s)) $r (return (pair $r raw)))))

; get-atoms returns Atom: its stored callable remains data. Duplicate stored
; occurrences survive, and a protocol sentinel restores only this invocation.
!(let $s (new-space)
   (let $u (add-atom $s (foo 4))
    (let $u (add-atom $s (foo 4)) (collapse (get-atoms $s)))))
!(let $s (new-space)
   (let $u (add-atom $s NotReducible)
    (case (get-atoms $s) (((get-atoms $s) restored) (NotReducible completed)))))
!(let $s (new-space)
   (let $u (add-atom $s NotReducible)
    (case (metta (get-atoms $s) %Undefined% &self)
      (((get-atoms $s) restored) ((metta (get-atoms $s) %Undefined% &self) outer)
       (NotReducible completed)))))
!(let $s (new-space)
   (let $u (add-atom $s NotReducible)
    (function (chain (eval (get-atoms $s)) $r (return (pair $r raw))))))

; Return interpretation retains caller bindings and the selected result type.
(= (bind-read A) (value A))
!(let $s (plant (bind-read $caller)) (let $r (get-state $s) (pair $r $caller)))
(: number-read (-> Number Number))
(= (number-read $x) (+ $x 1))
!(let $s (plant (number-read 3)) (metta (get-state $s) Number &self))
!(let $s (plant (number-read 3))
   (let $r (metta (get-state $s) $type &self) (pair $r $type)))
!(let $type $alias
   (let $s (plant (number-read 3))
    (let $r (metta (get-state $s) $type &self) (pair $r $type $alias))))
!(let $s (plant (number-read 3))
   (case (metta (get-state $s) String &self)
     (((Error (get-state $s) (BadType String Number)) rejected))))

; The stored call executes once only in the raw-producer profile.
!(bind! &count (new-state 0))
(: typed-tick (-> Number))
(= (typed-tick) (let $u (change-state! &count 99) 7))
!(let $s (plant (typed-tick))
   (case (metta (get-state $s) String &self)
     (((Error (get-state $s) (BadType String Number)) rejected))))
!(get-state &count)
(= (tick) (let $u (change-state! &count (+ (get-state &count) 1)) ticked))
!(let $s (plant (tick)) (pair (get-state &count) (get-state $s)))
!(get-state &count)

; Raw grounded execution reports its own error; ordinary typed application
; rejects the argument at the signature boundary before invoking that reader.
!(case (eval (get-state 1))
   (((Error (get-state 1) $message) (get-metatype $message))))

; A malformed minimal unify is observed three ways: the message symbol,
; the source subject, and a four-argument success. Each stays its own query.
(= (unify-message) (function (chain (unify $a A B) $r (return $r))))
!(case (unify-message)
  (((Error $s IncorrectNumberOfArguments) code)
   ($other text)))
(= (unify-subject) (function (chain (unify $a A B) $r (return $r))))
!(case (unify-subject)
  (((Error (unify $v A B) $m) subject)
   ($other other)))
(= (unify-success) (function (chain (unify A A yes no) $r (return $r))))
!(case (unify-success)
  ((yes hit)
   ($other miss)))
