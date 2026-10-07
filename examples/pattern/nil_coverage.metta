; Premises carry proofs in Nil's nilbc format: (: proof theorem).
; A conjunctive query supplies one substitution for the whole proof step.
!(import! &self pat)
(: ab (Parent alice bob))
(: ab (Parent alice bob))
(: bc (Parent bob carol))
(: open (Parent diane $unknown))
(: ef (Parent erin frank))

(= (grandparents $kb)
   (pat:match% $kb
     (, (: $first (Parent $x $y)) (: $second (Parent $y $z)))
     (: (transitive $first $second) (Grandparent $x $z))))
; Two equal proof occurrences remain two witnesses. The open row cannot
; acquire erin merely to satisfy the second premise.
!(collapse (grandparents &self))

; The same constraint batch also works without a temporary space.
!(pat:apply
   (pat:solve match% (((P $x) (P a)) ((Q $x) (Q a))))
   (covered $x))
!(collapse (pat:solve match% (((P $x) (P a)) ((Q $x) (Q b)))))
; Capturing an open subject does not grant permission to bind it later.
!(collapse (pat:solve match% (((P $x) (P $subject)) ((Q $x) (Q a)))))
