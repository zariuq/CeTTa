; One operation corpus shared by the native C ABT bridge, the canonical CeTTa
; waist, and its generated PeTTa companion.  Lane-specific preludes define the
; four oracle operations; this file owns only the terms being compared.

!(println! (ABTWaistOracle s01
  (oracle-shift 1 0 (idx 0))))
!(println! (ABTWaistOracle s02
  (oracle-shift 1 0
    (App (Lam (Con nat) (idx 1)) (idx 0)))))
!(println! (ABTWaistOracle s03
  (oracle-shift 1 0
    (Pi (Con nat) (App (Con vec) (idx 0))))))
!(println! (ABTWaistOracle s04
  (oracle-shift 1 0
    (Lam (Con nat) (App (idx 0) (idx 1))))))
!(println! (ABTWaistOracle s05
  (oracle-shift-args 1 0
    (ArgsCons (idx 0)
      (ArgsCons (Lam (Con nat) (idx 1)) ArgsNil)))))
!(println! (ABTWaistOracle s06
  (oracle-shift-cases 1 0
    (Cases (Case z (idx 0))
      (Cases (Case s (Lam (Con nat) (idx 1))) CasesNil)))))
!(println! (ABTWaistOracle s07
  (oracle-shift 1 0
    (IndG nat
      (ArgsCons (idx 0) ArgsNil)
      (Lam (Con nat) (Con nat))
      (Cases (Case z (idx 0)) CasesNil)
      (ArgsCons (idx 0) ArgsNil)
      (idx 0)))))
!(println! (ABTWaistOracle s08
  (oracle-shift 1 0
    (NatRec (idx 0) (Con z)
      (Lam (Con nat) (idx 1)) (idx 0)))))
!(println! (ABTWaistOracle s09
  (oracle-subst 0 (Con z)
    (App (Lam (Con nat) (idx 1)) (idx 0)))))
!(println! (ABTWaistOracle s10
  (oracle-subst 0 (idx 0)
    (Lam (Con nat) (idx 1)))))
!(println! (ABTWaistOracle s11
  (oracle-subst 0 (Con z) (idx 2))))
!(println! (ABTWaistOracle s12
  (oracle-depth-shift (idx 3))))
!(println! (ABTWaistOracle s13
  (oracle-depth-subst (idx 0) (idx 3))))

!(println! (ABTWaistOracleSummary 13 13 0))
