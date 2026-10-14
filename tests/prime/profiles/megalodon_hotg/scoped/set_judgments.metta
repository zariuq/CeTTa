(hol:profile megalodon-hotg)

; `hol:` judgments are `type:` judgments in an environment extended by an
; admitted set-theoretic signature, followed by a descent certificate that the
; operands lie in the constant-family (simple) fragment.  Outside-fragment
; operands and unknown names stay inert; only proved negatives become False.

!(hol:formed set)
!(hol:formed (-> set prop))
!(hol:of Empty)
!(hol:of In)
!(hol:check (In Empty Empty) prop)
!(hol:check (lam x (In x Empty)) (-> set prop))
!(hol:check Empty prop)
!(hol:eq (In Empty Empty) (In Empty Empty))
!(hol:eq (In Empty Empty) (In Empty (Power Empty)))

; A proposition is a term of type `prop`, not a type: a dependent arrow whose
; codomain is a proposition is not formed, and the judgment stays inert.
!(hol:formed (-> (x : set) (In x x)))

; Only the judgment spellings are owned by the bubble.  Any other prefixed
; name is ordinary vocabulary and keeps its equations.
(= (set:twice $f $x) ($f ($f $x)))
!(set:twice Power Empty)

; Proofs are checked bidirectionally against a closed proposition; the tower
; kernel owns the conversion between synthesized and expected propositions.
!(hol:proves (pf:imp-intro (pf:hyp 0)) (imp (In Empty Empty) (In Empty Empty)))
!(hol:proves (pf:all-intro (pf:imp-intro (pf:hyp 0)))
             (all set (lam x (imp (In x x) (In x x)))))
!(hol:proves (pf:all-elim (pf:known universeIn) Empty) (In Empty (UnivOf Empty)))
!(hol:proves (pf:imp-intro (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty) (pf:hyp 0)))
             (imp (In Empty Empty) Falsum))

; Higher-order work across the interface: quantification over propositions,
; instantiation with a variable bound by an enclosing proof binder, and open
; witnesses built from signature constants.
!(hol:proves (pf:all-intro (pf:imp-intro (pf:hyp 0))) (all prop (lam p (imp p p))))
!(hol:proves (pf:all-intro (pf:all-elim (pf:known universeIn) (pf:var 0)))
             (all set (lam x (In x (UnivOf x)))))
!(hol:proves (pf:all-intro (pf:all-elim (pf:known universeIn) (Power (pf:var 0))))
             (all set (lam x (In (Power x) (UnivOf (Power x))))))
!(hol:proves (pf:all-intro (pf:all-intro (pf:imp-intro (pf:hyp 0))))
             (all set (lam x (all prop (lam p (imp p p))))))
; An ill-typed open witness is a checked obstruction.
!(hol:proves (pf:all-intro (pf:all-elim (pf:known universeIn) (In (pf:var 0))))
             (all set (lam x (In x (UnivOf x)))))

; Checked obstructions are refutations; missing names are abstentions.
!(hol:proves (pf:hyp 0) (In Empty Empty))
!(hol:proves (pf:imp-intro (pf:hyp 0)) (imp (In Empty Empty) Falsum))
!(hol:proves (pf:known nothing) Falsum)

; Publication retains the proof and the cited dependencies.  Reuse replays
; the dependencies against the current space: a removed or changed premise
; makes reuse inert, never silently True.
; Signature definitions remain pinned by the signature digest, separately
; from these selected context dependencies.
!(hol:theorem empty-not-self (imp (In Empty Empty) Falsum)
              (pf:imp-intro (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty) (pf:hyp 0))))
!(hol:known-proposition empty-not-self)
!(hol:known-proof empty-not-self)
!(match &self (hol:known empty-not-self $p $origin $proof $depends $revision $sig)
        (Published $origin $depends))
!(hol:recheck empty-not-self)
!(hol:axiom assumed (In Empty Empty))
!(hol:theorem derived Falsum (pf:imp-elim (pf:known empty-not-self) (pf:known assumed)))
!(hol:proves (pf:known derived) Falsum)
!(match &self (hol:known assumed $p $o $pf $d $r $sig)
        (remove-atom &self (hol:known assumed $p $o $pf $d $r $sig)))
!(hol:proves (pf:known derived) Falsum)
!(hol:recheck derived)
!(hol:axiom assumed (In Empty (Power Empty)))
!(hol:proves (pf:known derived) Falsum)
; A user record that shadows a seeded name with another proposition makes the
; name ambiguous rather than selecting either.
!(hol:axiom universeIn (In Empty Empty))
!(hol:proves (pf:known universeIn) (In Empty Empty))
!(hol:axiom bad-axiom (In Empty))
