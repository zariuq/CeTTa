!(import! &self ../lib/lib_wmpln)

; Golden parity checks for the Lean PeTTa truth-function audit:
; confidence-weight conversion, induction/abduction weight-space min,
; modus-ponens product confidence, negation preservation, and revision as
; weight addition.

!(assertEqual (Truth_c2w 0.5) 1.0)
!(assertEqual (Truth_c2w -0.5) 0.0)
!(assertEqual (Truth_w2c 3.0) 0.75)
!(assertEqual (Truth_w2c -3.0) 0.0)

!(assertEqual (Truth_Induction (stv 0.5 0.1)
                               (stv 0.5 0.2)
                               (stv 0.5 0.3)
                               (stv 0.5 0.5)
                               (stv 0.5 0.75))
              (stv 0.5 0.5))
!(assertEqual (Truth_Abduction (stv 0.5 0.1)
                               (stv 0.5 0.2)
                               (stv 0.5 0.3)
                               (stv 0.5 0.5)
                               (stv 0.5 0.75))
              (stv 0.5 0.5))

!(assertEqual (Truth_ModusPonens (stv 0.5 0.5) (stv 0.5 0.5))
              (stv 0.26 0.25))
!(assertEqual (* 0.5 0.5) 0.25)

!(assertEqual (Truth_Negation (stv 0.25 0.75)) (stv 0.75 0.75))
!(assertEqual (Truth_Revision (stv 0.5 0.5) (stv 0.5 0.5))
              (stv 0.5 0.6666666666666666))
!(assertEqual (Truth_Revision (stv 0.2 0.5) (stv 0.8 0.75))
              (stv 0.6500000000000001 0.8))
