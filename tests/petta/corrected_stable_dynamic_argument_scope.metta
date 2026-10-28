; Settled application/revision probe derived from corrected stable dynamic_argument_scope.
; Eager argument cuts commit the caller; literal Atom arguments remain held.
(= (scope-id $x) $x)
(: scope-hold (-> Atom %Undefined%))
(= (scope-hold $x) $x)
(: scope-checked (-> Number Number))
(= (scope-checked $x) $x)
(= (scope-choice $f) (let $_ ($f (progn (cut) 1)) left))
(= (scope-choice $f) right)
!(test (collapse (scope-choice scope-id)) (left))
!(test (collapse (scope-choice scope-checked)) (left))
!(test (collapse (scope-choice scope-hold)) (left right))

!(bind! &scope-count (new-state 0))
(= (scope-tick)
   (let $_ (change-state! &scope-count (+ (get-state &scope-count) 1)) 7))
(: scope-atom-tag Atom)
(: scope-same (-> $t $t Bool))
(= (scope-same $x $y) True)

; A type variable becoming Atom or %Undefined% is still a checked variable.
!(test (collapse (let $f scope-same ($f scope-atom-tag (scope-tick)))) ())
!(test (get-state &scope-count) 1)
!(test (collapse (let $f scope-same ($f untyped-value 42))) ())
!(test (collapse (let $f scope-same ($f 42 untyped-value))) ())
(: scope-wrong-result (-> $t $t))
(= (scope-wrong-result $x) 42)
!(test (collapse (let $f scope-wrong-result ($f scope-atom-tag))) ())

; Bound partial arguments do not change the remaining arguments' demand.
!(test (collapse (let $p (scope-same scope-atom-tag) ($p (scope-tick)))) ())
!(test (get-state &scope-count) 2)

; A call-shaped value arriving through a variable or reduce stays a value.
(: scope-echo (-> %Undefined% %Undefined%))
(= (scope-echo $x) (Kept $x))
!(test (let* (($f scope-echo) ($x (quote (scope-tick)))) ($f $x))
       (quote (Kept (scope-tick))))
!(test (reduce (scope-echo (quote (scope-tick)))) (quote (Kept (scope-tick))))
!(test (get-state &scope-count) 2)

; The signature is selected before an argument's declaration-changing effect.
(: scope-changing (-> Number %Undefined%))
(= (scope-changing $x) (Changed $x))
(= (scope-change-argument)
   (progn (remove-atom &self (: scope-changing (-> Number %Undefined%)))
          (add-atom &self (: scope-changing (-> Atom %Undefined%))) 7))
!(test (let $f scope-changing ($f (scope-change-argument))) (Changed 7))
!(test (let $f scope-changing ($f (+ 1 2))) (quote (Changed (+ 1 2))))

; Arity rejection precedes argument effects, just as for a direct typed call.
!(test (let (Error $reason $context)
            (catch (let $f scope-checked ($f (scope-tick) 2))) rejected)
       rejected)
!(test (get-state &scope-count) 2)
