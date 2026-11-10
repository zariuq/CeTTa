; Optimization: programs with minimize statements, their optimal stable
; models.  Fixed controls, then seeded programs whose optimum, found by branch
; and bound, is compared with the least cost vector over every stable model of
; the program without its minimize statements (the highest priority first),
; and whose asp:models are exactly the models of that cost.
!(import! &self asp)

(= (canon-each $ms) (if (== $ms ()) () (let ($h $t) (decons-atom $ms) (cons-atom (sort $h) (canon-each $t)))))
(= (canon $ms) (sort (canon-each $ms)))
(= (costs-of $r) (let (optimum $m $c $p) $r ((sort $m) $c (size-atom $p))))

; {a; b; c}, a or b, costs 3, 2 and 1: b alone
!(costs-of (asp:solve ((choice a () ()) (choice b () ()) (choice c () ())
                       (constraint () (a b))
                       (minimize 0 ((a 3) (b 2) (c 1)) ()))))
; two priorities: the higher first, then the lower among its optima
!(costs-of (asp:solve ((choice p () ()) (choice q () ()) (choice r () ())
                       (minimize 2 ((p 1)) ((q 1)))
                       (minimize 1 ((r 5)) ((r 2))))))
; a negative weight: choosing a lowers the cost, no refutation needed
!(costs-of (asp:solve ((choice a () ()) (minimize 0 ((a -2)) ()))))
; no stable model: the refutation
!(car-atom (asp:solve ((rule a () ()) (constraint (a) ()) (minimize 0 ((a 1)) ()))))
!(let (models $ms $c) (asp:models ((rule a () ()) (constraint (a) ()) (minimize 0 ((a 1)) ()))) $ms)
; the optimal models, every one
!(let (models $ms $cert) (asp:models ((choice a () ()) (choice b () ()) (constraint () (a b))
                                      (minimize 0 ((a 1) (b 1)) ())))
   (canon $ms))

;; ---- seeded programs against brute force ----------------------------------
(= (next $s) (% (* $s 48271) 2147483647))
(= (pick $s $xs) (index-atom $xs (% $s (size-atom $xs))))
(= (atoms) (a b c d e))

(= (statement $s)
   (let* (($s1 (next $s)) ($s2 (next $s1)) ($s3 (next $s2)) ($s4 (next $s3))
          ($x (pick $s1 (atoms))) ($y (pick $s2 (atoms))) ($z (pick $s3 (atoms)))
          ($k (% $s4 6)))
     (if (< $k 3) (choice $x () ())
     (if (== $k 3) (rule $x ($y) ($z))
     (if (== $k 4) (weight-constraint 2 (($x 1) ($y 1)) ())
                   (constraint ($x) ($y)))))))

(= (minimize-of $s)
   (let* (($s1 (next $s)) ($s2 (next $s1)) ($s3 (next $s2)) ($s4 (next $s3)) ($s5 (next $s4))
          ($x (pick $s1 (atoms))) ($y (pick $s2 (atoms)))
          ($w1 (pick $s3 (1 2 3 -1))) ($w2 (pick $s4 (1 2 4)))
          ($p (pick $s5 (0 0 1))))
     (minimize $p (($x $w1)) (($y $w2)))))

(= (program $s $n $m)
   (if (== $n 0)
       (if (== $m 0) ()
         (cons-atom (minimize-of $s) (program (next (next (next $s))) 0 (- $m 1))))
       (cons-atom (statement $s) (program (next (next (next $s))) (- $n 1) $m))))

(= (without-minimize $p)
   (if (== $p ()) ()
     (let ($st $more) (decons-atom $p)
       (if (== (car-atom $st) minimize) (without-minimize $more)
         (cons-atom $st (without-minimize $more))))))

(= (member-of $x $xs)
   (if (== $xs ()) False
     (let ($y $more) (decons-atom $xs) (if (== $x $y) True (member-of $x $more)))))

(= (weigh $m $pos $neg)
   (+ (weigh-pos $m $pos) (weigh-neg $m $neg)))
(= (weigh-pos $m $ts)
   (if (== $ts ()) 0
     (let* ((($t $more) (decons-atom $ts)) (($a $w) $t))
       (+ (if (member-of $a $m) $w 0) (weigh-pos $m $more)))))
(= (weigh-neg $m $ts)
   (if (== $ts ()) 0
     (let* ((($t $more) (decons-atom $ts)) (($a $w) $t))
       (+ (if (member-of $a $m) 0 $w) (weigh-neg $m $more)))))

; the cost of model m at priority q: every minimize statement at q
(= (cost-at $m $q $p)
   (if (== $p ()) 0
     (let ($st $more) (decons-atom $p)
       (if (and (== (car-atom $st) minimize) (== (index-atom $st 1) $q))
           (+ (weigh $m (index-atom $st 2) (index-atom $st 3)) (cost-at $m $q $more))
           (cost-at $m $q $more)))))

(= (vector $m $p) ((cost-at $m 1 $p) (cost-at $m 0 $p)))

(= (lex< $u $v)
   (let* ((($u1 $u0) $u) (($v1 $v0) $v))
     (if (< $u1 $v1) True (if (== $u1 $v1) (< $u0 $v0) False))))

(= (least $ms $p)
   (let ($m $more) (decons-atom $ms)
     (if (== $more ()) (vector $m $p)
       (let* (($v (vector $m $p)) ($w (least $more $p)))
         (if (lex< $v $w) $v $w)))))

(= (at-cost $ms $p $v)
   (if (== $ms ()) ()
     (let ($m $more) (decons-atom $ms)
       (if (== (vector $m $p) $v) (cons-atom $m (at-cost $more $p $v)) (at-cost $more $p $v)))))

; the optimum's cost at priority q, as asp:solve gives it
(= (at-priority $q $c)
   (if (== $c ()) 0
     (let ($e $more) (decons-atom $c)
       (let ($pr $cost) $e (if (== $pr $q) $cost (at-priority $q $more))))))

(= (check $p)
   (let (models $all $c0) (asp:models (without-minimize $p))
     (if (== $all ()) (car-atom (asp:solve $p))
       (let* (($best (least $all $p))
              ((optimum $m $c $proofs) (asp:solve $p))
              ((models $opt $c1) (asp:models $p)))
         (agree (== (vector $m $p) $best)
                (== ((at-priority 1 $c) (at-priority 0 $c)) $best)
                (== (canon $opt) (canon (at-cost $all $p $best))))))))

(= (scenarios $seed $n)
   (if (== $n 0) ()
     (let* (($k (+ 3 (% $seed 4)))
            ($_ (println! ($seed (check (program (next $seed) $k 2))))))
       (scenarios (next (next (next (next $seed)))) (- $n 1)))))

!(scenarios 11 120)
