; Settled application/revision probe derived from corrected stable forward_application_plans.
; Installed bodies, rather than provisional source signatures, decide arity.
(= (fap-pair $x $y) (pair $x $y))
(= (fap-full) (fap-forward 1 2))
(= (fap-forward $x) (fap-pair $x))
!(test (fap-full) (pair 1 2))

(: fap-typed (-> Number Number Number))
(= (fap-add $x $y) (+ $x $y))
(= (fap-early) (fap-typed 40))
(= (fap-typed $x) (fap-add $x))
!(test ((fap-early) 2) 42)

; Until the body exists, preserve the expression without a final-result guard.
(: fap-not-yet (-> Number Number Number))
(= (fap-pending $x) (fap-not-yet $x))
!(test (fap-pending 40) (quote (fap-not-yet 40)))
(= (fap-not-yet $x) (fap-add $x))
!(test ((fap-pending 40) 2) 42)

; Expansion propagates through more than one forward alias.
(= (fap-chain-user) (fap-first 40 2))
(= (fap-first $x) (fap-second $x))
(= (fap-second $x) (fap-base $x))
(= (fap-base $x $y) (+ $x $y))
!(test (fap-chain-user) 42)

; Earlier and later consumers follow removal and a new executable interface.
(= (fap-before) (fap-target 5))
(= (fap-target $x) (+ $x 1))
(= (fap-after) (fap-target 5))
!(test (fap-before) 6)
!(test (fap-after) 6)
!(remove-atom &self (= (fap-target $x) (+ $x 1)))
!(test (fap-before) (quote (fap-target 5)))
!(test (fap-after) (quote (fap-target 5)))
!(add-atom &self (= (fap-target $x $y) (+ $x $y)))
!(test ((fap-before) 2) 7)
!(test ((fap-after) 2) 7)

; An argument can install a forward body while its caller is already running.
(: fap-inflight-target (-> Number Number Number))
(= (fap-inflight)
   (let $u (add-atom &self (= (fap-inflight-target $x) (fap-add $x)))
     (fap-inflight-target 40)))
!(test ((fap-inflight) 2) 42)
(= (fap-inflight-target $x) (fap-add $x))

; Saturated delegation follows only the called source arity, not a shorter
; overload of the same name. This also terminates cyclic arity inquiries.
(= (fap-delegate $x $options) done)
(= (fap-delegate $x) (fap-delegate $x ()))
!(test (fap-delegate 1) done)
!(test (let (Error $reason $context) (catch (fap-delegate 1 2 3)) rejected) rejected)
(= (fap-loop $x) (fap-loop $x))
!(test (let (Error $reason $context) (catch (fap-loop 1 2)) rejected) rejected)
