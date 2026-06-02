; lts:he debugging showcase
; =========================
; Run with:
;   ./cetta --profile he-extended --lang he examples/lts_he_debugging_showcase.metta
;
; This example shows how `lts:he` helps debug evaluator behavior by exposing
; one-step successors as quoted states.

!(import! &self lts:he)

; A nondeterministic choice and a tiny pipeline.
(= (choose-color) red)
(= (choose-color) blue)
(= (paint $c) (finish $c))

!(println! "=== branching frontier ===")
!(println!
  ("chain (choose-color) ->"
    (collapse (lts:he:transitions
      (chain (choose-color) $c (paint $c))))))

; The frontier contains quoted intermediate states, so we can inspect them
; without triggering another evaluation step immediately.
!(println! "=== inspect one branch ===")
!(println!
  ("chain red ->"
    (collapse (lts:he:transitions
      (chain red $c (paint $c))))))

!(println! "=== inspect the next reduction ===")
!(println!
  ("paint red ->"
    (collapse (lts:he:transitions (paint red)))))

; This is useful for telling apart:
; - reducible states
; - quiescent/stuck states
; - genuinely finished results
!(println! "=== can-step vs normal-form ===")
!(println! ("chain red can-step?" (lts:he:is-can-step (chain red $c (paint $c)))))
!(println! ("finish red normal-form?" (lts:he:is-normal-form (finish red))))
!(println! ("unknown-fn normal-form?" (lts:he:is-normal-form (unknown-fn a))))

!(println! "=== quiescent frontier ===")
!(println!
  ("unknown-fn ->"
    (collapse (lts:he:transitions (unknown-fn a)))))
