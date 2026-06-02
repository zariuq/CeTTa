; lts:he reconvergence showcase
; ============================
; Run with:
;   ./cetta --profile he-extended --lang he examples/lts_he_reconvergence_showcase.metta
;
; This example shows how `lts:he` helps with debugging nondeterministic control
; flow that branches and then reconverges.

!(import! &self lts:he)

(= (choose-path) left)
(= (choose-path) right)

!(println! "=== one-step branch ===")
!(println!
  ("let choose-path ->"
    (collapse (lts:he:transitions
      (let $p (choose-path) done)))))

!(println! "=== exact two-step reconvergence traces ===")
!(println!
  ("trace-2 ->"
    (collapse (lts:he:trace-2
      (let $p (choose-path) done)))))

!(println! "=== quoted-state stepping ===")
!(println!
  ("step-state of left branch ->"
    (collapse (lts:he:step-state
      (quote (let $p left done))))))

!(println! "=== why this is useful ===")
!(println! "Both branches remain visible as intermediate states, but the second step shows that they reconverge to the same final state `done`.")
