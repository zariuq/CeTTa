; Disjunctions, read by shifting: a head-cycle-free program keeps its stable
; models, each the minimal choice of heads its bodies force; one whose head
; atoms depend positively on each other is refused.  Each set of models sorted.
!(import! &self asp)
(= (canon-each $ms) (if (== $ms ()) () (let ($h $t) (decons-atom $ms) (cons-atom (sort $h) (canon-each $t)))))
(= (canon $ms) (sort (canon-each $ms)))
(= (models-of $p) (let (models $ms $cert) (asp:models $p) (canon $ms)))
; a | b: one of them, never both
!(models-of ((disjunction (a b) () ())))
; a | b | c :- not d, with d chosen freely: either d, or exactly one head
!(models-of ((disjunction (a b c) () (d)) (choice d () ())))
; a head written twice is one head: a | a :- not b. b :- not a.
!(models-of ((disjunction (a a) () (b)) (rule b () (a))))
; a disjunction whose body fails forces nothing
!(models-of ((disjunction (a b) (c) ())))
; a constraint on every head leaves no model, with a certificate
!(let $r (asp:solve ((disjunction (a b) () ()) (constraint (a) ()) (constraint (b) ()))) (car-atom $r))
; a body atom that a head derives keeps the program head-cycle-free
!(models-of ((disjunction (a b) () ()) (rule c (a) ()) (rule d (b) ())))
; a and b depending on each other positively: not head-cycle-free, refused
!(asp:models ((disjunction (a b) () ()) (rule a (b) ()) (rule b (a) ())))
; one head depending on another through a third atom: refused as well
!(asp:models ((disjunction (a b) () ()) (rule c (a) ()) (rule b (c) ()) (rule a (b) ())))
