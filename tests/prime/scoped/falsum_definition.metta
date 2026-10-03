; Falsum is a definition of the admitted set: signature, the proposition that
; implies every proposition: Falsum := all prop (lam p p).  Every space knows
; it with its defining equation, it computes by that equation in proofs and in
; set:eq, and nothing proves it outright.
(: q prop)
(: opaque prop)

; The definition as the signature publishes it.
!(set:known-proposition Falsum)
!(set:known-proof Falsum)
!(set:eq Falsum (all prop (lam p p)))
!(set:formed (-> prop prop))

; Ex falso: from a hypothesis Falsum, any proposition, by unfolding and
; instantiating the quantifier.
!(set:proves (pf:imp-intro (pf:all-elim (pf:hyp 0) q)) (imp Falsum q))
!(set:theorem ex-falso (all prop (lam r (imp Falsum r)))
   (pf:all-intro (pf:imp-intro (pf:all-elim (pf:hyp 0) (pf:var 0)))))
!(match &self (set:known ex-falso $p theorem $proof $depends $r $s)
   (ExFalsoDependencies $depends))
!(set:proves (pf:all-elim (pf:known ex-falso) (In Empty Empty)) (imp Falsum (In Empty Empty)))

; Double negation introduction, with negation defined over the signature's
; Falsum by a space.
!(set:define neg (-> prop prop) (= (neg $p) (imp $p Falsum)))
!(set:proves (pf:all-intro (pf:imp-intro (pf:imp-intro (pf:imp-elim (pf:hyp 0) (pf:hyp 1)))))
   (all prop (lam p (imp p (neg (neg p))))))
!(set:proves (pf:all-intro (pf:imp-intro (pf:imp-intro (pf:imp-elim (pf:hyp 0) (pf:hyp 1)))))
   (all prop (lam p (imp p (neg p)))))

; The law emptyLaw still concludes Falsum, and so now eliminates into any
; proposition.
!(set:proves (pf:imp-intro (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty) (pf:hyp 0)))
   (imp (In Empty Empty) Falsum))
!(set:proves (pf:imp-intro (pf:all-elim (pf:imp-elim (pf:all-elim (pf:known emptyLaw) Empty) (pf:hyp 0)) q))
   (imp (In Empty Empty) q))

; The native package of ex falso is checked by the kernel with the definition's
; rule.
!(let $package (set:native-proof ex-falso) (car-atom $package))

; No closed proof of Falsum is accepted.
!(set:proves (pf:imp-intro (pf:hyp 0)) Falsum)
!(set:proves (pf:all-intro (pf:hyp 0)) Falsum)
!(set:proves (pf:known emptyLaw) Falsum)
!(set:theorem bottom-proved Falsum (pf:all-intro (pf:hyp 0)))

; The same ex-falso proof does not eliminate an opaque proposition.
!(set:proves (pf:imp-intro (pf:all-elim (pf:hyp 0) q)) (imp opaque q))

; A space can neither redefine it nor displace it with a record of its own.
!(let $r (try (set:define Falsum prop (= Falsum (imp q q)))) (car-atom $r))

; A fact checked against the signature before Falsum was defined abstains:
; its record names the old signature's digest.
!(== (set:signature-digest)
     "4af51ee31207779bb184b387aea7a147203e9be9f6a71ddeb7c6dfd0c64367e7")
!(add-atom &self (set:known old-falsum-fact (imp Falsum q) theorem
                            (pf:imp-intro (pf:all-elim (pf:hyp 0) q)) () 0
                            "4af51ee31207779bb184b387aea7a147203e9be9f6a71ddeb7c6dfd0c64367e7"))
!(try (set:proves (pf:known old-falsum-fact) (imp Falsum q)))
!(try (set:recheck old-falsum-fact))
; Published again under the current signature, the same statement is reused.
!(set:theorem new-falsum-fact (imp Falsum q) (pf:imp-intro (pf:all-elim (pf:hyp 0) q)))
!(set:proves (pf:known new-falsum-fact) (imp Falsum q))

; A record inserted under the name, claiming another definition, is not
; admitted: Falsum keeps computing by the signature's equation, and the name's
; record is ambiguous rather than replaced.
!(let $d (set:signature-digest)
   (add-atom &self (set:known Falsum (define prop (= Falsum (imp q q))) definition
                              (definition-evidence (rules 0 (() (App (App imp q) q) 0)) (non-recursive))
                              () 0 $d)))
!(set:proves (pf:imp-intro (pf:all-elim (pf:hyp 0) q)) (imp Falsum q))
!(set:proves (pf:imp-intro (pf:hyp 0)) Falsum)
!(let $r (try (set:known-proposition Falsum)) (car-atom $r))
