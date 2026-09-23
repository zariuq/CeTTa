; `set:` judgments are `type:` judgments in an environment extended by an
; admitted set-theoretic signature, followed by a descent certificate that the
; operands lie in the constant-family (simple) fragment.  Outside-fragment
; operands and unknown names stay inert; only proved negatives become False.

!(set:formed set)
!(set:formed (-> set prop))
!(set:of Empty)
!(set:of In)
!(set:check (In Empty Empty) prop)
!(set:check (lam x (In x Empty)) (-> set prop))
!(set:check Empty prop)
!(set:eq (In Empty Empty) (In Empty Empty))
!(set:eq (In Empty Empty) (In Empty (Power Empty)))

; A proposition is a term of type `prop`, not a type: a dependent arrow whose
; codomain is a proposition is not formed, and the judgment stays inert.
!(set:formed (-> (x : set) (In x x)))

; Only the judgment spellings are owned by the bubble.  Any other prefixed
; name is ordinary vocabulary and keeps its equations.
(= (set:twice $f $x) ($f ($f $x)))
!(set:twice Power Empty)

; Proofs are checked bidirectionally against a closed proposition; the tower
; kernel owns the conversion between synthesized and expected propositions.
!(set:proves (pf:imp-intro (pf:hyp 0)) (imp (In Empty Empty) (In Empty Empty)))
!(set:proves (pf:all-intro (pf:imp-intro (pf:hyp 0)))
             (all set (lam x (imp (In x x) (In x x)))))
!(set:proves (pf:all-elim (pf:known PowerI) Empty) (In Empty (Power Empty)))
!(set:proves (pf:imp-intro (pf:imp-elim (pf:all-elim (pf:known EmptyE) Empty) (pf:hyp 0)))
             (imp (In Empty Empty) Falsum))

; Higher-order work across the interface: quantification over propositions,
; instantiation with a variable bound by an enclosing proof binder, and open
; witnesses built from signature constants.
!(set:proves (pf:all-intro (pf:imp-intro (pf:hyp 0))) (all prop (lam p (imp p p))))
!(set:proves (pf:all-intro (pf:all-elim (pf:known PowerI) (pf:var 0)))
             (all set (lam x (In x (Power x)))))
!(set:proves (pf:all-intro (pf:all-elim (pf:known PowerI) (Power (pf:var 0))))
             (all set (lam x (In (Power x) (Power (Power x))))))
!(set:proves (pf:all-intro (pf:all-intro (pf:imp-intro (pf:hyp 0))))
             (all set (lam x (all prop (lam p (imp p p))))))
; An ill-typed open witness is a checked obstruction.
!(set:proves (pf:all-intro (pf:all-elim (pf:known PowerI) (In (pf:var 0))))
             (all set (lam x (In x (Power x)))))

; Checked obstructions are refutations; missing names are abstentions.
!(set:proves (pf:hyp 0) (In Empty Empty))
!(set:proves (pf:imp-intro (pf:hyp 0)) (imp (In Empty Empty) Falsum))
!(set:proves (pf:known nothing) Falsum)

; Publication retains the proof and the cited dependencies.  Reuse replays
; the dependencies against the current space: a removed or changed premise
; makes reuse inert, never silently True.
!(set:theorem empty-not-self (imp (In Empty Empty) Falsum)
              (pf:imp-intro (pf:imp-elim (pf:all-elim (pf:known EmptyE) Empty) (pf:hyp 0))))
!(set:known-proposition empty-not-self)
!(set:known-proof empty-not-self)
!(match &self (set:known empty-not-self $p $origin $proof $depends $revision $sig)
        (Published $origin $depends))
!(set:recheck empty-not-self)
!(set:axiom assumed (In Empty Empty))
!(set:theorem derived Falsum (pf:imp-elim (pf:known empty-not-self) (pf:known assumed)))
!(set:proves (pf:known derived) Falsum)
!(match &self (set:known assumed $p $o $pf $d $r $sig)
        (remove-atom &self (set:known assumed $p $o $pf $d $r $sig)))
!(set:proves (pf:known derived) Falsum)
!(set:recheck derived)
!(set:axiom assumed (In Empty (Power Empty)))
!(set:proves (pf:known derived) Falsum)
; A user record that shadows a seeded name with another proposition makes the
; name ambiguous rather than selecting either.
!(set:axiom PowerI (In Empty Empty))
!(set:proves (pf:known PowerI) (In Empty Empty))
!(set:axiom bad-axiom (In Empty))
