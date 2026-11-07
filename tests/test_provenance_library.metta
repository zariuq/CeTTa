; The provenance library: supports of derivations in rule programs (HE).
!(import! &self provenance)
; p(x,y) <- e(x,y) is r1; p(x,y) <- e(x,z), p(z,y) is r2
(= (path-rules) ((rule r1 (lit p 0 1) ((lit e 0 1))) (rule r2 (lit p 0 1) ((lit e 0 2) (lit p 2 1)))))
(= (line) ((fact e a b) (fact e b c)))
; a to c takes r2 then r1: two steps, one support
!(let $r (path-rules) (let $f (line) (provenance:supports $f $r ((goal p a c)) (steps 2))))
; within one step there is none
!(let $r (path-rules) (let $f (line) (provenance:supports $f $r ((goal p a c)) (steps 1))))
; an open output: a to b by r1, a to c by r2 and r1
!(let $r (path-rules) (let $f (line) (provenance:supports $f $r ((goal p a $y)) (steps 3))))
; on a cycle, steps count every use of a rule, labels count distinct rules
(= (cycle) ((fact e a b) (fact e b a)))
!(let $r (path-rules) (let $f (cycle) (provenance:supports $f $r ((goal p a a)) (steps 2))))
!(let $r (path-rules) (let $f (cycle) (provenance:supports $f $r ((goal p a a)) (steps 4))))
!(let $r (path-rules) (let $f (cycle) (provenance:supports $f $r ((goal p a a)) (labels 2))))
!(let $r (path-rules) (let $f (cycle) (provenance:supports $f $r ((goal p a a)) (labels 1))))
; alternatives: two rules for the same step give two supports, and labels are
; sorted in the standard order whatever order the rules come in
!(provenance:supports ((fact e a b)) ((rule (via 2) (lit q 0 1) ((lit e 0 1))) (rule (via 1) (lit q 0 1) ((lit e 0 1))) (rule top (lit t 0 1) ((lit q 0 1)))) ((goal t a b)) (steps 2))
; any arity: even(x) <- zero(x) is z; even(x) <- s(y, x), odd(y) is e;
; odd(x) <- s(y, x), even(y) is o
(= (parity) ((rule z (lit even 0) ((lit zero 0))) (rule e (lit even 0) ((lit s 1 0) (lit odd 1))) (rule o (lit odd 0) ((lit s 1 0) (lit even 1)))))
(= (numbers) ((fact zero n0) (fact s n0 n1) (fact s n1 n2) (fact s n2 n3) (fact s n3 n4)))
!(let $r (parity) (let $f (numbers) (provenance:supports $f $r ((goal even n4)) (steps 5))))
!(let $r (parity) (let $f (numbers) (provenance:supports $f $r ((goal even n3)) (steps 5))))
!(let $r (parity) (let $f (numbers) (provenance:supports $f $r ((goal even $n)) (labels 2))))
; a three-place relation: plus(x, y, z) from successor facts
(= (plus) ((rule p0 (lit plus 0 1 1) ((lit zero 0) (lit nat 1))) (rule ps (lit plus 0 1 2) ((lit s 3 0) (lit plus 3 1 4) (lit s 4 2))) (rule nz (lit nat 0) ((lit zero 0))) (rule ns (lit nat 0) ((lit s 1 0) (lit nat 1)))))
!(let $r (plus) (let $f (numbers) (provenance:supports $f $r ((goal plus n2 n1 n3)) (labels 4))))
; a predicate is its name and its arity, as in Prolog: e of two arguments
; gives p of two, e of one gives p of one
!(provenance:supports ((fact e a b) (fact e a)) ((rule r1 (lit p 0 1) ((lit e 0 1))) (rule r2 (lit p 0) ((lit e 0)))) ((goal p a b) (goal p a)) (steps 1))
; labels from data: one rule stands for its labelled instances, each
; instance taking the label of the fact its label literal matches
(= (hop-rules) ((rule-from 1 (lit p 0 2) ((lit e 0 1 2) (lit via 1)))))
(= (hop-facts) ((fact e a k1 b) (fact e a k2 b) (fact e b k3 c) (labelled (by k1) via k1) (labelled (by k2) via k2) (labelled (by k3) via k3)))
!(let $r (hop-rules) (let $f (hop-facts) (provenance:supports $f $r ((goal p a $y)) (steps 1))))
; one computation, one family per goal group
!(let $r (path-rules) (let $f (line) (provenance:supports-each $f $r (((goal p a b)) ((goal p a c)) ((goal p b c) (goal p a c))) (steps 2))))
; a goal on a predicate or node the program never mentions has no supports
!(let $r (path-rules) (let $f (line) (provenance:supports $f $r ((goal p z c) (goal w a c)) (steps 4))))
; a malformed program is refused (HE ends a script at its first error, so the
; other refusals are checked in tests/petta/provenance_library.metta)
!(provenance:supports () () () (depth 2))
