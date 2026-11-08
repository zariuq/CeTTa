; The asp library in the PeTTa lane; see tests/test_asp_library.metta.
!(import! &self asp)
; an even loop has two stable models
!(asp:solve ((rule p () (q)) (rule q () (p))))
!(asp:models ((rule p () (q)) (rule q () (p))))
; p :- not p has none: a certificate refutes it
!(asp:solve ((rule p () (p))))
; atoms are any ground atoms
!(asp:solve ((rule (on a) () ()) (rule (lit b) ((on a)) ())))
; a positive loop without outside support stays false
!(asp:models ((rule p (q) ()) (rule q (p) ()) (rule r () ())))
; choice rules and integrity constraints: a or b, not both, and not b
!(asp:models ((choice a () ()) (choice b () ()) (constraint () (a b)) (constraint (a b) ()) (constraint (b) ())))
; a constraint that no model escapes
!(asp:solve ((rule p () ()) (constraint (p) ())))
; programs are ground
!(asp:solve ((rule p ($x) ())))
; the solver's steps: an even loop decided, a positive loop unfounded
!(asp:trace ((rule a () (b)) (rule b () (a)) (rule c (b) ())))
!(asp:trace ((rule p (q) ()) (rule q (p) ()) (rule r () (p))))
; a trace ends as the solver answers
!(let $solved (asp:solve ((rule a () (b)) (rule b () (a)) (constraint (a) ()))) (let (trace $atoms $steps $result) (asp:trace ((rule a () (b)) (rule b () (a)) (constraint (a) ()))) (== $result $solved)))
; the variants of a body under the injective maps of its variables into 1 .. 3
!(asp:injections ((p 0 $a) (q $a $b)) ($a $b) (1 2 3))
; without variables to map the term itself; too few values, none
!(asp:injections ((p 0 0)) () (1 2))
!(asp:injections ((p $a $b $c)) ($a $b $c) (1 2))
; variables only, and distinct
!(asp:injections (p $a) (x) (1 2))
!(asp:injections (p $a) ($a $a) (1 2))
