!(import! &self (library lib_transform))
(= (show $s) (sort-atom (collapse (match $s $a $a))))

; rho COMM: take the message and the receiver together; the continuation runs after the commit
!(bind! &rho (new-space))
(= (greet $m) (let $_ (add-atom &rho (send reply (hello $m))) (greeted $m)))
!(add-atom &rho (send ch1 alice))
!(add-atom &rho (recv ch1 greet))
!(comm (collapse (transform &rho (, (take (send $c $m)) (take (recv $c $f))) ($f $m))))
!(comm-store (show &rho))
!(comm-again (collapse (transform &rho (, (take (send $c $m)) (take (recv $c $f))) ($f $m))))

; peek: the message is read, the receiver taken
!(bind! &pk (new-space))
!(add-atom &pk (send ch1 bob))
!(add-atom &pk (peek ch1 greet))
!(add-atom &pk (peek ch1 greet))
!(peek1 (collapse (transform &pk (, (send $c $m) (take (peek $c $f))) (seen-by-peek $m))))
!(peek2 (collapse (transform &pk (, (send $c $m) (take (peek $c $f))) (seen-by-peek $m))))
!(peek3 (collapse (transform &pk (, (send $c $m) (take (peek $c $f))) (seen-by-peek $m))))
!(peek-store (show &pk))

; persistent receive: the contract is read, every message taken in one commit
!(bind! &pr (new-space))
!(add-atom &pr (contract ch2 greet))
!(add-atom &pr (send ch2 carol))
!(add-atom &pr (send ch2 dave))
!(served (sort-atom (collapse (transform &pr (, (contract $c $f) (every (take (send $c $m)))) (served $m)))))
!(served-store (show &pr))

; crossed two-fork join: the second philosopher finds no fork
!(bind! &t (new-space))
!(add-atom &t (fork 1))
!(add-atom &t (fork 2))
!(forks-a (transform &t (, (take (fork 1)) (take (fork 2))) (eating A) &t))
!(forks-b (collapse (transform &t (, (take (fork 2)) (take (fork 1))) (eating B) &t)))
!(forks-store (show &t))

; job claim
!(bind! &q (new-space))
!(add-atom &q (job a))
!(add-atom &q (job b))
!(claim (collapse (transform &q (take (job $j)) (claimed $j))))
!(claim-store (show &q))

; one coin, two buyers: every buyer taking the same coin is refused as a whole;
; one buyer at a time succeeds once
!(bind! &m (new-space))
!(add-atom &m (coin c1))
!(add-atom &m (wants u1))
!(add-atom &m (wants u2))
!(coin-every (collapse (transform &m (, (every (wants $u)) (take (coin $c))) (bought $u $c) &m)))
!(coin-each (transform &m (, (wants $u) (take (coin $c))) (bought $u $c) &m))
!(coin-each-again (collapse (transform &m (, (wants $u) (take (coin $c))) (bought $u $c) &m)))
!(coin-store (show &m))

; write skew: A reads x and takes y; B reads y and takes x.  In sequence B finds y gone.
!(bind! &w (new-space))
!(add-atom &w (x))
!(add-atom &w (y))
!(skew-a (transform &w (, (x) (take (y))) (P) &w))
!(skew-b (collapse (transform &w (, (y) (take (x))) (Q) &w)))
!(skew-store (show &w))

; identification: reading and taking one atom needs two copies
!(bind! &d (new-space))
!(add-atom &d (tok))
!(dpo-one (collapse (transform &d (, (read (tok)) (take (tok))) (got) &d)))
!(add-atom &d (tok))
!(dpo-two (collapse (transform &d (, (read (tok)) (take (tok))) (got) &d)))
!(dpo-store (show &d))

; two takes of equal atoms need two copies; a bag keeps its other copies
!(bind! &b (new-space))
!(add-atom &b (cell))
!(add-atom &b (cell))
!(add-atom &b (cell))
!(two-takes (transform &b (, (take (cell)) (take (cell))) (pair) &b))
!(two-takes-store (show &b))
!(two-takes-again (collapse (transform &b (, (take (cell)) (take (cell))) (pair) &b)))
