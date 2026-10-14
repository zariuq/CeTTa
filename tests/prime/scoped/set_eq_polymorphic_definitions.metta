(hol:profile core)

; A definition whose body uses the polymorphic quantifier or equality converts
; under hol:eq exactly as under the proof checker.  Its stored rule keeps the
; source's head, which ranges over every type of `class` and takes no
; universe argument; only the proof route specializes it, where it selects
; rules.
!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile core)) $profile-space)))
!(add-atom &thy (: A prop))
!(hol:define &thy falsum prop (= falsum (all prop (lam p p))))
!(hol:define &thy refl-A prop (= refl-A (eq prop A A)))
!(hol:define &thy mono prop (= mono (imp A A)))
!(hol:define &thy over-sets prop (= over-sets (all (-> prop prop) (lam f (eq prop (f A) (f A))))))

!(hol:eq &thy falsum (all prop (lam p p)))
!(hol:eq &thy refl-A (eq prop A A))
!(hol:eq &thy falsum falsum)
!(hol:eq &thy mono (imp A A))
!(hol:eq &thy over-sets (all (-> prop prop) (lam g (eq prop (g A) (g A)))))

; Distinct bodies stay apart.
!(hol:eq &thy mono (imp A mono))
!(hol:eq &thy falsum (all prop (lam p (imp p p))))
!(hol:eq &thy refl-A (eq prop A (imp A A)))

; The stored rules.
!(match &thy (type:rule falsum $n $pats $rhs) $rhs)
!(match &thy (type:rule refl-A $n $pats $rhs) $rhs)
!(match &thy (type:rule mono $n $pats $rhs) $rhs)

; The proof checker agrees in both directions.
!(hol:proves &thy (pf:imp-intro (pf:hyp 0)) (imp falsum (all prop (lam p p))))
!(hol:proves &thy (pf:imp-intro (pf:hyp 0)) (imp (all prop (lam p p)) falsum))
!(hol:proves &thy (pf:imp-intro (pf:hyp 0)) (imp refl-A (eq prop A A)))
