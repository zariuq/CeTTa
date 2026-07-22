; Chapter 1: `=` introduces a directional rewrite rule
;
; In executable MeTTa, `(= lhs rhs)` is not a proposition asserting symmetric
; equality.  The loader stores it as a rule occurrence.  When a call matches
; `lhs`, the evaluator returns the correspondingly substituted `rhs` and then
; continues evaluating that result.
;
; Matching may bind variables on either side of the matcher interface, but the
; rewrite direction remains lhs -> rhs.  The runtime never indexes the RHS as a
; way to run the rule backward.

(= (rewrite-tutorial:color red) warm)
(= (rewrite-tutorial:color blue) cool)

!(rewrite-tutorial:color red)
!(rewrite-tutorial:color blue)

; No LHS matches green, so the call remains an unreduced residual expression.
!(rewrite-tutorial:color green)

; `==` is a strict computed equality predicate.  It is a different operation
; from the top-level rule marker `=`.
!(== 2 2)
!(== 2 3)

; `noreduce-eq` compares syntax without first reducing the arguments.  It too
; is an expression-level predicate, not a rewrite declaration.
!(noreduce-eq (node (+ 1 1)) (node (+ 1 1)))
!(noreduce-eq (node (+ 1 1)) (node 2))
