!(import! &self pat:clause)
(clause broad ((pos (P $x))))
(clause linked ((pos (P $x)) (pos (Q $x))))
(clause linked ((pos (P $x)) (pos (Q $x))))
(clause crossed ((pos (P a)) (pos (Q b))))
(clause negative ((neg (P $x))))

; Forward: which existing clauses cover the new clause?
; The occurrence identifies the supporting evidence: the two identical linked
; clauses remain distinct. A template containing only their values loses this
; provenance. The clause witness is reusable on its original literal syntax.
(= (forward $kb $new)
   (let (pat:hit ((pat:row $occ (clause $name $old) $view)) $row-witness)
        (pat:query $kb match% (clause $label $body))
     (let $w (pat:subsume set $old $new)
       (cover $occ $name $old (pat:apply $w $old)))))

; Backward: which existing clauses does the new clause cover?
(= (backward $kb $new)
   (let (pat:hit ((pat:row $occ (clause $name $old) $view)) $row-witness)
        (pat:query $kb match% (clause $label $body))
     (let $w (pat:subsume set $new $old)
       (cover $occ $name $old (pat:apply $w $new)))))

; Literal order is irrelevant, but one substitution must cover every literal.
!(collapse (forward &self ((pos (Q a)) (pos (P a)))))
!(collapse (backward &self ((pos (P $candidate)))))
; Occurrence-sensitive coverage is an explicit different policy.
!(collapse
   (let $w (pat:subsume set ((pos (P $x)) (pos (P $y))) ((pos (P a))))
     (pat:apply $w (covered $x $y))))
!(collapse
   (pat:subsume multiset ((pos (P $x)) (pos (P $y))) ((pos (P a)))))
; Finding logical redundancy does not remove evidence occurrences.
!(collapse (match &self (clause $name $body) $name))
