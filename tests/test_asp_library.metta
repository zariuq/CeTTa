; The asp library: ground answer-set programs as MeTTa data.
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
; the solver's steps: an even loop decided, a positive loop unfounded
!(asp:trace ((rule a () (b)) (rule b () (a)) (rule c (b) ())))
!(asp:trace ((rule p (q) ()) (rule q (p) ()) (rule r () (p))))
; a trace ends as the solver answers
!(let $solved (asp:solve ((rule a () (b)) (rule b () (a)) (constraint (a) ()))) (let (trace $atoms $steps $result) (asp:trace ((rule a () (b)) (rule b () (a)) (constraint (a) ()))) (== $result $solved)))
; weight rules: at least two of three, and a weighted body with a negated literal
!(asp:models ((choice a () ()) (choice b () ()) (choice c () ()) (weight-constraint 3 ((a 1) (b 1) (c 1)) ()) (weight-rule two 2 ((a 1) (b 1) (c 1)) ()) (constraint () (two))))
!(asp:models ((choice a () ()) (weight-rule p 3 ((a 2)) ((b 2)))))
; programs are ground
!(asp:solve ((rule p ($x) ())))
