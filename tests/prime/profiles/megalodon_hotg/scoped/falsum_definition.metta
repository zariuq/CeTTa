(hol:profile megalodon-hotg)

; Falsum is a definition of the admitted hol: signature, the proposition that
; implies every proposition: Falsum := all prop (lam p p).  Every space knows
; it with its defining equation, it computes by that equation in proofs and in
; hol:eq, and nothing proves it outright.
(: q prop)
(: opaque prop)

; The definition as the signature publishes it.
!(hol:known-proposition Falsum)
!(hol:known-proof Falsum)
!(hol:eq Falsum (all prop (lam p p)))
!(hol:formed (-> prop prop))

; Ex falso: from a hypothesis Falsum, any proposition, by unfolding and
; instantiating the quantifier.
!(hol:proves (pf:imp-intro (pf:all-elim (pf:hyp 0) q)) (imp Falsum q))
!(hol:theorem ex-falso (all prop (lam r (imp Falsum r)))
   (pf:all-intro (pf:imp-intro (pf:all-elim (pf:hyp 0) (pf:var 0)))))
; The exact built-in definition belongs to the receipt's signature digest;
; this theorem uses no additional context dependency.
!(match &self (hol:known ex-falso $p theorem $proof $depends $r $s)
   (ExFalsoDependencies $depends))
!(hol:proves (pf:all-elim (pf:known ex-falso) (In Empty Empty)) (imp Falsum (In Empty Empty)))

; Double negation introduction, with negation defined over the signature's
; Falsum by a space.
!(hol:define neg (-> prop prop) (= (neg $p) (imp $p Falsum)))
!(hol:proves (pf:all-intro (pf:imp-intro (pf:imp-intro (pf:imp-elim (pf:hyp 0) (pf:hyp 1)))))
   (all prop (lam p (imp p (neg (neg p))))))
!(hol:proves (pf:all-intro (pf:imp-intro (pf:imp-intro (pf:imp-elim (pf:hyp 0) (pf:hyp 1)))))
   (all prop (lam p (imp p (neg p)))))

; The law emptyLaw still concludes Falsum, and so now eliminates into any
; proposition.
!(hol:proves (pf:imp-intro (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty) (pf:hyp 0)))
   (imp (In Empty Empty) Falsum))
!(hol:proves (pf:imp-intro (pf:all-elim (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty) (pf:hyp 0)) q))
   (imp (In Empty Empty) q))

; The native package of ex falso is checked by the kernel with the definition's
; rule.
!(let $package (hol:native-proof ex-falso) (car-atom $package))

; No closed proof of Falsum is accepted.
!(hol:proves (pf:imp-intro (pf:hyp 0)) Falsum)
!(hol:proves (pf:all-intro (pf:hyp 0)) Falsum)
!(hol:proves (pf:known emptyLaw) Falsum)
!(hol:theorem bottom-proved Falsum (pf:all-intro (pf:hyp 0)))

; The same ex-falso proof does not eliminate an opaque proposition.
!(hol:proves (pf:imp-intro (pf:all-elim (pf:hyp 0) q)) (imp opaque q))

; A space can neither redefine it nor displace it with a record of its own.
!(let $r (try (hol:define Falsum prop (= Falsum (imp q q)))) (car-atom $r))

; A fact checked against the signature before Falsum was defined abstains:
; its record names the old signature's digest.
!(== (hol:signature-digest)
     "4af51ee31207779bb184b387aea7a147203e9be9f6a71ddeb7c6dfd0c64367e7")
!(add-atom &self (hol:known old-falsum-fact (imp Falsum q) theorem
                            (pf:imp-intro (pf:all-elim (pf:hyp 0) q)) () 0
                            "4af51ee31207779bb184b387aea7a147203e9be9f6a71ddeb7c6dfd0c64367e7"))
!(try (hol:proves (pf:known old-falsum-fact) (imp Falsum q)))
!(try (hol:recheck old-falsum-fact))
; Published again under the current signature, the same statement is reused.
!(hol:theorem new-falsum-fact (imp Falsum q) (pf:imp-intro (pf:all-elim (pf:hyp 0) q)))
!(hol:proves (pf:known new-falsum-fact) (imp Falsum q))

; A record inserted under the name, claiming another definition, is not
; admitted: Falsum keeps computing by the signature's equation, and the name's
; record is ambiguous rather than replaced.
!(let $d (hol:signature-digest)
   (add-atom &self (hol:known Falsum (define prop (= Falsum (imp q q))) definition
                              (definition-evidence (rules 0 (() (App (App imp q) q) 0)) (non-recursive))
                              () 0 $d)))
!(hol:proves (pf:imp-intro (pf:all-elim (pf:hyp 0) q)) (imp Falsum q))
!(hol:proves (pf:imp-intro (pf:hyp 0)) Falsum)
!(let $r (try (hol:known-proposition Falsum)) (car-atom $r))
