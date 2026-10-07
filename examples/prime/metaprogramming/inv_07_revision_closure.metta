(set:profile hol)

; Revision-aware definitions.
;
; A published fact is reusable only while everything it was checked against
; is unchanged: the selected typed declarations, admitted computation rules,
; definitions it mentions and, transitively, the bodies of the definitions
; those mention.  Changing a definition behind an alias whose
; own equation is unchanged is a change; restoring the body restores reuse; an
; unrelated definition changes nothing.

!(bind! &theory (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(add-atom &theory (: A prop))
!(add-atom &theory (: B prop))

; Withdraw one admitted definition: its record, its rule and its declaration.
(= (withdraw $space $name)
   (let $known (collapse (match $space (set:known $name $p $o $proof $deps $r $s)
                            (remove-atom $space (set:known $name $p $o $proof $deps $r $s))))
     (let $rules (collapse (match $space (type:rule $name $n $pats $rhs)
                              (remove-atom $space (type:rule $name $n $pats $rhs))))
       (let $decls (collapse (match $space (: $name $t) (remove-atom $space (: $name $t))))
         (Withdrawn $name)))))

; 1-4. root -> middle -> leaf = (imp A A), and a theorem about root.
!(set:define &theory leaf prop (= leaf (imp A A)))
!(set:define &theory middle prop (= middle leaf))
!(set:define &theory root prop (= root middle))
!(set:theorem &theory saved root (pf:imp-intro (pf:hyp 0)))

; 5. The fact records the whole closure it was checked against.
!(match &theory (set:known saved $p $o $proof $deps $r $s) (Relied-on $deps))

; 6. While the closure is current the fact is reusable.
!(try (set:proves &theory (pf:known saved) root))

; 7-8. Change the body of leaf only.  middle and root keep their equations.
!(withdraw &theory leaf)
!(set:define &theory leaf prop (= leaf (all prop (lam p p))))

; 9-11. Every use of the old fact now abstains, naming the changed dependency:
;       reuse, recheck and native compilation.
!(try (set:proves &theory (pf:known saved) root))
!(try (set:recheck &theory saved))
!(try (set:native-proof &theory saved))

; 12. The old proof would now "prove" (all p. p); it is not reusable for it.
!(try (set:proves &theory (pf:known saved) (all prop (lam p p))))

; 13-15. Restoring the old body restores reuse.
!(withdraw &theory leaf)
!(set:define &theory leaf prop (= leaf (imp A A)))
!(try (set:proves &theory (pf:known saved) root))

; 16-17. An unrelated definition changes nothing that was relied on.
!(set:define &theory unrelated prop (= unrelated (imp B B)))
!(try (set:proves &theory (pf:known saved) root))

; 18-20. Change leaf again, to (imp B B).  An alias over a changed body is
;        itself stale until it is renewed.
!(withdraw &theory leaf)
!(set:define &theory leaf prop (= leaf (imp B B)))
!(try (set:recheck &theory middle))

; 21-25. Renew both aliases.  Every alias is now current, yet the fact still
;        names the old body of leaf, so it is not revived.
!(withdraw &theory middle)
!(set:define &theory middle prop (= middle leaf))
!(withdraw &theory root)
!(set:define &theory root prop (= root middle))
!(try (set:proves &theory (pf:known saved) root))

; 26-29. Published again (a plain call: `try` publishes nothing), the same
;        proof is checked against the new body and reused; the recorded
;        closure now names (imp B B).
!(withdraw &theory saved)
!(set:theorem &theory saved root (pf:imp-intro (pf:hyp 0)))
!(try (set:proves &theory (pf:known saved) root))
!(match &theory (set:known saved $p $o $proof $deps $r $s) (Relied-on $deps))
