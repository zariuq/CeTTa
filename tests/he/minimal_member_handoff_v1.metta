; A minimal instruction hands acquired member atoms to a held pattern.
; Matching interprets the substituted body, not the member or pattern.
(: mmh:hold (-> Atom Atom))
(= (mmh:hold $value) $value)

(= (mmh:call $value)
   (let $ignored (println! MEMBER-MUST-NOT-RUN) $value))

(: mmh:members (-> Atom Atom))
(= (mmh:members $items)
   (function
     (chain (eval (superpose $items)) $result
       (return $result))))

(: mmh:match (-> Atom Atom Atom %Undefined%))
(= (mmh:match $items $pattern $body)
   (let $pattern (mmh:members $items)
     (metta $body %Undefined% &self)))

; Callable members remain raw before and after structural matching.
!(println! (collapse (mmh:members ((mmh:call 1)))))
!(println! (collapse (mmh:match ((mmh:call 1)) (mmh:call $value) $value)))
!(println! (collapse (mmh:match ((mmh:call 1)) $item (mmh:hold $item))))

; Zero alternatives and a structural mismatch never activate the body.
!(println! (collapse (mmh:members ())))
!(println! (collapse (mmh:match () $item (println! EMPTY-BODY-MUST-NOT-RUN))))
!(println! (collapse (mmh:match (other) ($key $value) (println! MISMATCH-BODY-MUST-NOT-RUN))))

; Each occurrence is retained; failed unifications cannot bind later members.
!(println! (collapse (mmh:match (a a b) $item $item)))
!(println! (collapse (mmh:match ((a b) (a a) (b b) (a a)) ($x $x) $x)))

; Body effects follow member order and occur once per successful occurrence.
!(println! (collapse (mmh:match ((a 1) (b 2) (a 3)) ($key $value)
   (let $ignored (println! $key) ($key (+ $value 1))))))
