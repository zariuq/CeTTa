; Weight rules, as the verified normalization of the Lean development reads
; them: the stable models of its controls, each set sorted.
!(import! &self asp)
(= (canon-each $ms) (if (== $ms ()) () (let ($h $t) (decons-atom $ms) (cons-atom (sort $h) (canon-each $t)))))
(= (canon $ms) (sort (canon-each $ms)))
(= (models-of $p) (let (models $ms $cert) (asp:models $p) (canon $ms)))
(= (abc) ((choice a () ()) (choice b () ()) (choice c () ())))
; cardinality bounds 0, 1, 3 and 4 over a free choice of a, b, c
!(models-of (cons-atom (weight-rule p 0 ((a 1) (b 1) (c 1)) ()) (abc)))
!(models-of (cons-atom (weight-rule p 1 ((a 1) (b 1) (c 1)) ()) (abc)))
!(models-of (cons-atom (weight-rule p 3 ((a 1) (b 1) (c 1)) ()) (abc)))
!(models-of (cons-atom (weight-rule p 4 ((a 1) (b 1) (c 1)) ()) (abc)))
!(models-of (cons-atom (weight-constraint 2 ((a 1) (b 1) (c 1)) ()) (abc)))
!(models-of (cons-atom (weight-rule p 3 ((a 2) (c 1)) ((b 2))) (abc)))
!(models-of ((weight-rule p 1 () ((q 1))) (weight-rule q 1 () ((p 1)))))
!(models-of ((choice a () ()) (choice b () ()) (choice c () ()) (weight-rule two 2 ((a 1) (b 1) (c 1)) ()) (constraint () (two)) (weight-constraint 3 ((a 1) (b 1) (c 1)) ())))
!(models-of ((choice c () ()) (weight-choice (a b) 2 ((c 2)) ())))
!(models-of ((choice q () ()) (weight-rule p 2 ((p 1) (q 1)) ())))
!(models-of ((weight-rule p 2 ((a 1) (b 1)) ()) (rule a () ())))
; a negative weight is no weight statement
!(asp:solve ((weight-rule p 2 ((a -1)) ())))
