; C ABT operations must agree with the import-free MeTTa binding waist on the
; same canonical terms.  The harness prepends the authoritative waist source.

!(assertEqual
  (__cetta_abt_signature_admitted (AbtSignatures))
  True)
!(assertEqual
  (__cetta_abt_signature_admitted
    (AbtSignatures
      (AbtSignature Forall (Fields type body) (Bind1 body))))
  True)
!(assertEqual
  (__cetta_abt_signature_admitted
    (AbtSignatures
      (AbtSignature ScopedPair (Fields (Hole 0) (Hole 1))
        (BindFields
          (BCons (BField (Hole 0) 0)
            (BCons (BField (Hole 1) 2) BNil))))))
  True)
!(assertEqual
  (__cetta_abt_signature_admitted
    (AbtSignatures
      (AbtSignature BadDepth (Fields (Hole 0))
        (BindFields (BCons (BField (Hole 0) -1) BNil)))))
  False)
!(assertEqual
  (__cetta_abt_signature_admitted
    (AbtSignatures
      (AbtSignature Bad (Fields body body) Bind0)))
  False)
!(assertEqual
  (__cetta_abt_signature_admitted
    (AbtSignatures
      (AbtSignature Bad (Fields type body)
        (BindFields (BCons (BField type 0) BNil)))))
  False)

!(assertEqual
  (__cetta_abt_shift (AbtSignatures) 1 0 (Var 0))
  (bind-shift 1 0 (Var 0)))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures) 1 0
    (App (Lam (Con nat) (Var 1)) (Var 0)))
  (bind-shift 1 0
    (App (Lam (Con nat) (Var 1)) (Var 0))))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures) 1 0
    (Pi (Con nat) (App (Con vec) (Var 0))))
  (bind-shift 1 0
    (Pi (Con nat) (App (Con vec) (Var 0)))))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures) 1 0
    (Lam (Con nat) (App (Var 0) (Var 1))))
  (bind-shift 1 0
    (Lam (Con nat) (App (Var 0) (Var 1)))))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures) 1 0
    (ArgsCons (Var 0) (ArgsCons (Lam (Con nat) (Var 1)) ArgsNil)))
  (bind-shift-args 1 0
    (ArgsCons (Var 0) (ArgsCons (Lam (Con nat) (Var 1)) ArgsNil))))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures) 1 0
    (Cases (Case z (Var 0))
      (Cases (Case s (Lam (Con nat) (Var 1))) CasesNil)))
  (bind-shift-cases 1 0
    (Cases (Case z (Var 0))
      (Cases (Case s (Lam (Con nat) (Var 1))) CasesNil))))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures) 1 0
    (IndG nat
      (ArgsCons (Var 0) ArgsNil)
      (Lam (Con nat) (Con nat))
      (Cases (Case z (Var 0)) CasesNil)
      (ArgsCons (Var 0) ArgsNil)
      (Var 0)))
  (bind-shift 1 0
    (IndG nat
      (ArgsCons (Var 0) ArgsNil)
      (Lam (Con nat) (Con nat))
      (Cases (Case z (Var 0)) CasesNil)
      (ArgsCons (Var 0) ArgsNil)
      (Var 0))))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures) 1 0
    (NatRec (Var 0) (Con z) (Lam (Con nat) (Var 1)) (Var 0)))
  (bind-shift 1 0
    (NatRec (Var 0) (Con z) (Lam (Con nat) (Var 1)) (Var 0))))

!(assertEqual
  (__cetta_abt_subst
    (AbtSignatures) 0 (Con z)
    (App (Lam (Con nat) (Var 1)) (Var 0)))
  (bind-subst 0 (Con z)
    (App (Lam (Con nat) (Var 1)) (Var 0))))
!(assertEqual
  (__cetta_abt_subst
    (AbtSignatures) 0 (Var 0)
    (Lam (Con nat) (Var 1)))
  (bind-subst 0 (Var 0)
    (Lam (Con nat) (Var 1))))
!(assertEqual
  (__cetta_abt_subst
    (AbtSignatures) 0 (Con z) (Var 2))
  (bind-subst 0 (Con z) (Var 2)))
!(assertEqual
  (__cetta_abt_subst
    (AbtSignatures) 0 (Con z)
    (Cases (Case z (Var 0))
      (Cases (Case s (Lam (Con nat) (Var 1))) CasesNil)))
  (bind-subst-cases 0 (Con z)
    (Cases (Case z (Var 0))
      (Cases (Case s (Lam (Con nat) (Var 1))) CasesNil))))

; Package-declared constructors use the same field-depth laws.
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures
      (AbtSignature Forall (Fields type body) (Bind1 body)))
    1 0
    (Forall (Con nat) (Var 0)))
  (Forall (Con nat) (Var 0)))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures
      (AbtSignature Forall (Fields type body) (Bind1 body)))
    1 0
    (Forall (Con nat) (Var 1)))
  (Forall (Con nat) (Var 2)))
!(assertEqual
  (__cetta_abt_shift
    (AbtSignatures
      (AbtSignature ScopedPair (Fields left right)
        (BindFields
          (BCons (BField left 0)
            (BCons (BField right 2) BNil)))))
    1 0
    (ScopedPair (Var 0) (Var 2)))
  (ScopedPair (Var 1) (Var 3)))
!(assertEqual
  (__cetta_abt_subst
    (AbtSignatures
      (AbtSignature ScopedPair (Fields left right)
        (BindFields
          (BCons (BField left 0)
            (BCons (BField right 2) BNil)))))
    0 (Con z)
    (ScopedPair (Var 0) (Var 2)))
  (ScopedPair (Con z) (Con z)))

; Locally-nameless seams and canonical equality are neutral mechanisms.
!(assertEqual
  (__cetta_abt_close
    (AbtSignatures) x
    (App x (Lam (Con nat) x)))
  (App (Var 0) (Lam (Con nat) (Var 1))))
!(assertEqual
  (__cetta_abt_open
    (AbtSignatures) fresh
    (App (Var 0) (Lam (Con nat) (Var 1))))
  (App fresh (Lam (Con nat) fresh)))
!(assertEqual
  (__cetta_abt_close
    (AbtSignatures) (Free x)
    (App (Free x) (Lam (Con nat) (Free x))))
  (App (Var 0) (Lam (Con nat) (Var 1))))
!(assertEqual
  (__cetta_abt_open
    (AbtSignatures) (Free x)
    (App (Var 0) (Lam (Con nat) (Var 1))))
  (App (Free x) (Lam (Con nat) (Free x))))
!(assertEqual
  (__cetta_abt_scope_check
    (AbtSignatures) 0
    (Lam (Con nat) (Var 0)))
  True)
!(assertEqual
  (__cetta_abt_scope_check
    (AbtSignatures) 0
    (Lam (Con nat) (Var 1)))
  False)
!(assertEqual
  (__cetta_abt_alpha_eq
    (App (Var 0) (Lam (Con nat) (Var 1)))
    (App (Var 0) (Lam (Con nat) (Var 1))))
  True)
!(assertEqual
  (__cetta_abt_alpha_eq (Var 0) (Var 1))
  False)
!(assertEqual
  (__cetta_abt_print
    (AbtSignatures)
    (Lam (Con nat) (App (Var 0) a0)))
  (ABTBind Lam (Binders a1) (Con nat) (App a1 a0)))
!(assertEqual
  (__cetta_abt_parse
    (AbtSignatures)
    (ABTBind Lam (Binders x) (Con nat) (App x free)))
  (Lam (Con nat) (App (Var 0) free)))

!(println! (ABTDifferentialSummary 32 32 0))
