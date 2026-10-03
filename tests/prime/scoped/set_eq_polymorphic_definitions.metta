; A definition whose body uses the polymorphic quantifier or equality converts
; under set:eq exactly as under the proof checker.  Its stored rule keeps the
; source's head, which ranges over every type of `class` and takes no
; universe argument; only the proof route specializes it, where it selects
; rules.
!(bind! &thy (new-space))
!(add-atom &thy (: A prop))
!(set:define &thy falsum prop (= falsum (all prop (lam p p))))
!(set:define &thy refl-A prop (= refl-A (eq prop A A)))
!(set:define &thy mono prop (= mono (imp A A)))
!(set:define &thy over-sets prop (= over-sets (all (-> prop prop) (lam f (eq prop (f A) (f A))))))

!(set:eq &thy falsum (all prop (lam p p)))
!(set:eq &thy refl-A (eq prop A A))
!(set:eq &thy falsum falsum)
!(set:eq &thy mono (imp A A))
!(set:eq &thy over-sets (all (-> prop prop) (lam g (eq prop (g A) (g A)))))

; Distinct bodies stay apart.
!(set:eq &thy mono (imp A mono))
!(set:eq &thy falsum (all prop (lam p (imp p p))))
!(set:eq &thy refl-A (eq prop A (imp A A)))

; The stored rules.
!(match &thy (type:rule falsum $n $pats $rhs) $rhs)
!(match &thy (type:rule refl-A $n $pats $rhs) $rhs)
!(match &thy (type:rule mono $n $pats $rhs) $rhs)

; The proof checker agrees in both directions.
!(set:proves &thy (pf:imp-intro (pf:hyp 0)) (imp falsum (all prop (lam p p))))
!(set:proves &thy (pf:imp-intro (pf:hyp 0)) (imp (all prop (lam p p)) falsum))
!(set:proves &thy (pf:imp-intro (pf:hyp 0)) (imp refl-A (eq prop A A)))
