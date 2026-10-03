; A call to a definition by stored rules, typed.  `next` is defined by
; equations with a closed left side, stored in the space: two equal ones at
; a, which ask the edges that leave a, one at b, and one at c, whose right
; side is a node.  Run, a call gives the answers of the right side of every
; rule whose left side is the call, one copy for each rule.  Typed, the
; space declares the rules of `next` stored typed,
;
;     (type:stored (= (next node) node))
;
; so that its rules are the type next@rule, named by their argument and a
; digest of their right side (next@a@... twice, the second with #2);
; next@lhs gives a rule's argument, next@answers the type of the answers of
; its right side and next@returns the value each returns.  The answers of
; `(next x)` are a rule whose left side is the call with an answer of its
; right side,
;
;     (sigma (q : (sigma (r : next@rule) (id node (app next@lhs r) x)))
;            (app next@answers (fst q)))
;
; The right side of a rule is a typed query of the space, whose answers are
; its own, or a node v, whose one answer is (refl v).
(: node (u 0))
(: a node)
(: b node)
(: c node)
(type:stored (edge node node))
(edge a b)
(edge a c)
(edge b a)
(type:stored (= (next node) node))
(= (next a) (match &self (edge a $y) $y))
(= (next a) (match &self (edge a $y) $y))
(= (next b) (match &self (edge b $y) $y))
(= (next c) a)

; ---- Run: two rules at a, so b and c twice; one rule at c, so a.
!(next a)
!(next c)

; ---- Typed: the type of the call at a, and its answers.  Each of the two
; equal rules gives both answers of the query, so every answer is there
; twice, and each returns the target of its edge.  The rules at b and c are
; not selected.
!(type:of (next a))
!(let (Established (PrimeTypedQuery $type $answers $others))
      (try (type:of (next a)))
   $answers)
!(let (Established (PrimeTypedQuery $type $answers $others))
      (try (type:of (next a)))
   $others)
; At c the one rule's right side is a node: one answer, returning a.
!(let (Established (PrimeTypedQuery $type $answers $others))
      (try (type:of (next c)))
   $answers)

; ---- A key that is no node is no call of this definition.
!(try (type:of (next d)))

; ---- Negative control: a definition whose rules the space does not declare
; typed is typed as before, from the dynamic types of its equations.
(= (plain a) b)
(= (plain a) b)
!(plain a)
!(try (type:of (plain a)))
