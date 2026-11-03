; Membership in the HOTG set profile proves the positive dependent judgments but refutes
; nothing: every membership not derived from the declarations is consistent with them.
; Refutation needs a closure law: here the completion of one fibre of the family, with
; distinct names.  Both laws only constrain the new constants, and every model of HOTG
; expands to a model of them (interpret the names as distinct sets and each fibre as the
; set of its declared members), so they add no theorem in HOTG's own language.
(set:profile megalodon-hotg)
!(import! &self pf)
!(bind! &thy (let $s (new-space) (let $_ (add-atom $s (set:profile megalodon-hotg)) $s)))
!(pf:theory &thy ((: Fact set) (: fact set) (: other set) (: WitnessOf (-> set set))
                  (: goodWitness set) (: otherWitness set) (: Checked (-> set set set))
                  (: useWitness (-> set set set))))
; The declarations, read as memberships.
!(let $u (pf:axiom &thy d-fact (In fact Fact)) ())
!(let $u (pf:axiom &thy d-other (In other Fact)) ())
!(let $u (pf:axiom &thy d-good (In goodWitness (WitnessOf fact))) ())
!(let $u (pf:axiom &thy d-otherw (In otherWitness (WitnessOf other))) ())
; (: useWitness (-> (: $p Fact) (: $w (WitnessOf $p)) (Checked $p $w))) as a membership law.
!(let $u (pf:axiom &thy ap-use
   (all set (lam p (all set (lam w
     (imp (In p Fact) (imp (In w (WitnessOf p)) (In (useWitness p w) (Checked p w)))))))))
   ())

; The positive judgment is a derivation.
!(Positive
   (pf:proves &thy
     (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known ap-use) fact) goodWitness)
                               (pf:known d-fact))
                  (pf:known d-good))
     (In (useWitness fact goodWitness) (Checked fact goodWitness))))

; The refusal needs the premise refuted.  From the declarations alone no proof exists: they
; hold in a model where the fibre at fact contains otherWitness too.  Control: the refutation
; below, checked before the closure law is stated, is not accepted.
!(OpenWithoutLaw
   (let $verdict
     (pf:proves &thy
       (pf:imp-intro
         (pf:imp-elim (pf:known distinct-witnesses)
                      (pf:imp-elim (pf:all-elim (pf:known fibre-fact) otherWitness) (pf:hyp 0))))
       (imp (In otherWitness (WitnessOf fact)) Falsum))
     (if (== $verdict True) Accepted NotAccepted)))

; The refutation is exactly what the law adds: with the law as a hypothesis it is a theorem
; of the declarations.
!(RefutedUnderLaw
   (pf:proves &thy
     (pf:imp-intro (pf:imp-intro (pf:imp-intro
       (pf:imp-elim (pf:hyp 1)
                    (pf:imp-elim (pf:all-elim (pf:hyp 2) otherWitness) (pf:hyp 0))))))
     (imp (all set (lam x (imp (In x (WitnessOf fact)) (eq set x goodWitness))))
          (imp (imp (eq set otherWitness goodWitness) Falsum)
               (imp (In otherWitness (WitnessOf fact)) Falsum)))))

; The closure law: the fibre at fact has only its declared member, and the two witnesses
; are distinct.
!(let $u (pf:axiom &thy fibre-fact
   (all set (lam x (imp (In x (WitnessOf fact)) (eq set x goodWitness))))) ())
!(let $u (pf:axiom &thy distinct-witnesses (imp (eq set otherWitness goodWitness) Falsum)) ())
!(RefutedWithLaw
   (pf:proves &thy
     (pf:imp-intro
       (pf:imp-elim (pf:known distinct-witnesses)
                    (pf:imp-elim (pf:all-elim (pf:known fibre-fact) otherWitness) (pf:hyp 0))))
     (imp (In otherWitness (WitnessOf fact)) Falsum)))

; The law refutes only what it should: the declared witness is still a member.
!(StillPositive
   (pf:proves &thy
     (pf:imp-elim (pf:imp-elim (pf:all-elim (pf:all-elim (pf:known ap-use) fact) goodWitness)
                               (pf:known d-fact))
                  (pf:known d-good))
     (In (useWitness fact goodWitness) (Checked fact goodWitness))))
