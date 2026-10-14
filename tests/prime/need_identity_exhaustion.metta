; A program that meets an exhausted Need identity namespace.
;
; test-prime-need-identity-exhaustion runs this file once per namespace,
; with a cetta binary whose namespaces are placed near their end before the
; run (CETTA_TEST_PRIME_NEED_IDENTITY_SEED).  An identity is never issued
; twice: a query that needs one from an exhausted namespace reports itself
; incomplete, and a query that needs none still answers.

(= (double $x) (* 2 $x))

!(+ 1 1)
!(force (delay (+ 1 2)))
!(force (delay (+ 2 2)))
!(let $r (resample (+ 3 4)) (force $r))
!(double 5)
!(bind! &identity-counter (new-state 0))
!(change-state! &identity-counter 1)
!(get-state &identity-counter)
!(+ 2 2)
!(force (delay (+ 5 5)))
