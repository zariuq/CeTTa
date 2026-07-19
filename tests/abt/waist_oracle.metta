; One operation corpus shared by the native C ABT bridge, the canonical CeTTa
; waist, and its generated PeTTa companion.  Lane-specific preludes define the
; four oracle operations; this file owns only the terms being compared.

!(println! (ABTWaistOracle s01
  (oracle-shift 1 0 (Var 0))))
!(println! (ABTWaistOracle s02
  (oracle-shift 1 0
    (App (Lam (Con nat) (Var 1)) (Var 0)))))
!(println! (ABTWaistOracle s03
  (oracle-shift 1 0
    (Pi (Con nat) (App (Con vec) (Var 0))))))
!(println! (ABTWaistOracle s04
  (oracle-shift 1 0
    (Lam (Con nat) (App (Var 0) (Var 1))))))
!(println! (ABTWaistOracle s05
  (oracle-shift-args 1 0
    (ArgsCons (Var 0)
      (ArgsCons (Lam (Con nat) (Var 1)) ArgsNil)))))
!(println! (ABTWaistOracle s06
  (oracle-shift-cases 1 0
    (Cases (Case z (Var 0))
      (Cases (Case s (Lam (Con nat) (Var 1))) CasesNil)))))
!(println! (ABTWaistOracle s07
  (oracle-shift 1 0
    (IndG nat
      (ArgsCons (Var 0) ArgsNil)
      (Lam (Con nat) (Con nat))
      (Cases (Case z (Var 0)) CasesNil)
      (ArgsCons (Var 0) ArgsNil)
      (Var 0)))))
!(println! (ABTWaistOracle s08
  (oracle-shift 1 0
    (NatRec (Var 0) (Con z)
      (Lam (Con nat) (Var 1)) (Var 0)))))
!(println! (ABTWaistOracle s09
  (oracle-subst 0 (Con z)
    (App (Lam (Con nat) (Var 1)) (Var 0)))))
!(println! (ABTWaistOracle s10
  (oracle-subst 0 (Var 0)
    (Lam (Con nat) (Var 1)))))
!(println! (ABTWaistOracle s11
  (oracle-subst 0 (Con z) (Var 2))))
!(println! (ABTWaistOracle s12
  (oracle-depth-shift (Var 3))))
!(println! (ABTWaistOracle s13
  (oracle-depth-subst (Var 0) (Var 3))))

!(println! (ABTWaistOracleSummary 13 13 0))
