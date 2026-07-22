; Chapter 2: a call may admit several rewrite-rule occurrences
;
; For one call, every stored `(= lhs rhs)` occurrence whose LHS may match is a
; candidate.  Candidate identity is occurrence identity, not structural text:
; duplicate rules are still two admitted occurrences and retain multiplicity.

(= (rewrite-tutorial:classify A) specific-a)
(= (rewrite-tutorial:classify $x) (general $x))
!(rewrite-tutorial:classify (superpose (A B)))

; These two declarations have equal text but are distinct rule occurrences.
(= (rewrite-tutorial:duplicate A) duplicated)
(= (rewrite-tutorial:duplicate A) duplicated)
!(rewrite-tutorial:duplicate A)

; The first rule must inspect its argument to recognize A.  The wildcard rule
; can bind the still-delayed source and use it later.  Both subscribe to one
; dynamic source-argument cell; that sharing law is independent of how the
; candidate scheduler groups the rules.
(= (rewrite-tutorial:strict-wild A) strict)
(= (rewrite-tutorial:strict-wild $x) (wild $x))
!(rewrite-tutorial:strict-wild (superpose (A B)))
