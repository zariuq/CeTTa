(hol:profile base)

; Revision-aware definitions.
;
; A published fact is reusable only while everything it was checked against
; is unchanged: the selected typed declarations, admitted computation rules,
; definitions it mentions and, transitively, the bodies of the definitions
; those mention.  Changing a definition behind an alias whose
; own equation is unchanged is a change; restoring the body restores reuse; an
; unrelated definition changes nothing.

!(bind! &theory (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile base)) $profile-space)))
!(add-atom &theory (: A prop))
!(add-atom &theory (: B prop))

; Withdraw one admitted definition: its record, its rule and its declaration.
(= (withdraw $space $name)
   (let $known (collapse (match $space (hol:known $name $p $o $proof $deps $r $s)
                            (remove-atom $space (hol:known $name $p $o $proof $deps $r $s))){$name})
     (let $rules (collapse (match $space (type:rule $name $n $pats $rhs)
                              (remove-atom $space (type:rule $name $n $pats $rhs))){$name})
       (let $decls (collapse (match $space (: $name $t) (remove-atom $space (: $name $t))){$name})
         (Withdrawn $name)))))

; 1-4. root -> middle -> leaf = (imp A A), and a theorem about root.
!(hol:define &theory leaf prop (= leaf (imp A A)))
!(hol:define &theory middle prop (= middle leaf))
!(hol:define &theory root prop (= root middle))
!(hol:theorem &theory saved root (pf:imp-intro (pf:hyp 0)))

; 5. The fact records the whole closure it was checked against.
!(match &theory (hol:known saved $p $o $proof $deps $r $s) (Relied-on $deps))

; 6. While the closure is current the fact is reusable.
!(try (hol:proves &theory (pf:known saved) root))

; 7-8. Change the body of leaf only.  middle and root keep their equations.
!(withdraw &theory leaf)
!(hol:define &theory leaf prop (= leaf (all prop (lam p p))))

; 9-11. Every use of the old fact now abstains, naming the changed dependency:
;       reuse, recheck and native compilation.
!(try (hol:proves &theory (pf:known saved) root))
!(try (hol:recheck &theory saved))
!(try (hol:native-proof &theory saved))

; 12. The old proof would now "prove" (all p. p); it is not reusable for it.
!(try (hol:proves &theory (pf:known saved) (all prop (lam p p))))

; 13-15. Restoring the old body restores reuse.
!(withdraw &theory leaf)
!(hol:define &theory leaf prop (= leaf (imp A A)))
!(try (hol:proves &theory (pf:known saved) root))

; 16-17. An unrelated definition changes nothing that was relied on.
!(hol:define &theory unrelated prop (= unrelated (imp B B)))
!(try (hol:proves &theory (pf:known saved) root))

; 18-20. Change leaf again, to (imp B B).  An alias over a changed body is
;        itself stale until it is renewed.
!(withdraw &theory leaf)
!(hol:define &theory leaf prop (= leaf (imp B B)))
!(try (hol:recheck &theory middle))

; 21-25. Renew both aliases.  Every alias is now current, yet the fact still
;        names the old body of leaf, so it is not revived.
!(withdraw &theory middle)
!(hol:define &theory middle prop (= middle leaf))
!(withdraw &theory root)
!(hol:define &theory root prop (= root middle))
!(try (hol:proves &theory (pf:known saved) root))

; 26-29. Published again (a plain call: `try` publishes nothing), the same
;        proof is checked against the new body and reused; the recorded
;        closure now names (imp B B).
!(withdraw &theory saved)
!(hol:theorem &theory saved root (pf:imp-intro (pf:hyp 0)))
!(try (hol:proves &theory (pf:known saved) root))
!(match &theory (hol:known saved $p $o $proof $deps $r $s) (Relied-on $deps))
