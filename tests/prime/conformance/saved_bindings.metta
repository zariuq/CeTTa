; Saved answers retain logical identity across Prime continuations.
; These cases exercise grounded environments, separately from the legacy
; textual Bindings decoder.

(= (saved-proof routeA) (evidence sensor1))
(= (saved-proof routeA) (evidence sensor2))
(= (saved-proof routeB) (evidence sensor3))

; A saved domain cannot be rewritten by later substitutions. Restoration
; merges constraints: incompatibility removes only that alternative.
!(assertEqualToResult
  (chain (collapse-bind (unify $x A token Empty)) $saved
    (unify $x B
      (chain (superpose-bind $saved) $answer (choice $answer $x)) Empty))
  ())

; Compatible structured bindings refine variables in the captured range.
!(assertEqualToResult
  (chain (collapse-bind (unify $x (pair $y) token Empty)) $saved
    (unify $x (pair B)
      (chain (superpose-bind $saved) $answer (choice $answer $x $y)) Empty))
  ((choice token (pair B) B)))

!(assertEqualToResult
  (chain (collapse-bind (eval (saved-proof $route))) $saved
    (unify $route routeA
      (chain (superpose-bind $saved) $proof (plan $route $proof)) Empty))
  ((plan routeA (evidence sensor1)) (plan routeA (evidence sensor2))))

; Quoted syntax suspends evaluation but still carries logical variables.
!(assertEqualToResult
  (chain (collapse-bind (unify $piece (quote (f $x)) token Empty)) $saved
    (unify $x A
      (chain (superpose-bind $saved) $answer (quote (g $piece))) Empty))
  ((quote (g (quote (f A))))))

; Equal results from distinct proofs keep their multiplicity.
(= (saved-offer A proof1) accepted)
(= (saved-offer A proof2) accepted)
!(assertEqualToResult
  (chain (collapse-bind (eval (saved-offer $x $proof))) $saved
    (chain (superpose-bind $saved) $answer (choice $answer $x)))
  ((choice accepted A) (choice accepted A)))

; Restoration is repeatable and refines aliases by identity.
!(assertEqualToResult
  (chain (collapse-bind (unify $x $y token Empty)) $saved
    (unify $y A
      (chain (superpose-bind $saved) $first
        (chain (superpose-bind $saved) $second (both $first $second $x $y)))
      Empty))
  ((both token token A A)))

; A saved answer does not replay the effects which produced it.
!(assertEqualToResult
  (chain (collapse-bind (eval (add-atom &self (saved-event recorded)))) $saved
    (chain (superpose-bind $saved) $first
      (chain (superpose-bind $saved) $second done)))
  (done))
!(assertEqualToResult (match &self (saved-event $value) $value) (recorded))
