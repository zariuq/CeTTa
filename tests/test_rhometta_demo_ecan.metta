!(import! &self rhometta)

(hebbian a b 0.6) (hebbian a c 0.4)
(hebbian b d 0.5) (hebbian c d 0.5)
(hebbian d a 0.3) (hebbian d e 0.7)
(hebbian e f 1.0)

(= (max-spread) 0.5)

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

(= (sum-tuple $xs)
   (if (== $xs ()) 0.0 (+ (car-atom $xs) (sum-tuple (cdr-atom $xs)))))
(= (sti-of $x $vec)
   (if (== $vec ())
       0.0
       (let ($y $v) (car-atom $vec)
            (if (== $y $x) $v (sti-of $x (cdr-atom $vec))))))
(= (out-total $y)
   (sum-tuple (collapse (match &self (hebbian $y $z $w) $w))))
(= (inflow $x $vec)
   (sum-tuple (collapse
     (match &self (hebbian $y $x $w)
       (* (* (max-spread) (sti-of $y $vec)) (/ $w (out-total $y)))))))
(= (keep $x $vec)
   (if (> (out-total $x) 0.0)
       (* (- 1.0 (max-spread)) (sti-of $x $vec))
       (sti-of $x $vec)))
(= (new-sti $x $vec) ($x (+ (keep $x $vec) (inflow $x $vec))))

(= (cell $i $x $vec)
   (rho:par
     (rho:send (chan $i) (rhometta:eval (new-sti $x $vec)))
     (rho:recv (chan $i) $y (rho:drop $y))))
(= (round $vec)
   (rho:par (cell 1 a $vec) (cell 2 b $vec) (cell 3 c $vec)
            (cell 4 d $vec) (cell 5 e $vec) (cell 6 f $vec)))
(= (extract-vec (rho:par (rho:val $p1) (rho:val $p2) (rho:val $p3)
                         (rho:val $p4) (rho:val $p5) (rho:val $p6)))
   ($p1 $p2 $p3 $p4 $p5 $p6))
(= (spread $n $vec)
   (if (== $n 0)
       $vec
       (spread (- $n 1) (extract-vec (rhometta:run (round $vec))))))

; Pure-MeTTa reference: identical formulas, no rho orchestration.
(= (round-ref $vec)
   ((new-sti a $vec) (new-sti b $vec) (new-sti c $vec)
    (new-sti d $vec) (new-sti e $vec) (new-sti f $vec)))
(= (spread-ref $n $vec)
   (if (== $n 0) $vec (spread-ref (- $n 1) (round-ref $vec))))

(= (total-sti $vec) (sum-tuple (collapse (let ($x $v) (superpose $vec) $v))))
(= (sti0) ((a 60.0) (b 10.0) (c 10.0) (d 10.0) (e 5.0) (f 5.0)))

; rho-orchestrated parallel spreading equals the sequential reference.
!(assertEqual (spread 2 (sti0)) (spread-ref 2 (sti0)))

; ECAN economy invariant: left-stochastic spreading conserves total STI.
!(assertEqual (total-sti (spread 2 (sti0))) (total-sti (sti0)))

; Threaded round preserves the result.
!(pragma! num-threads 2)
!(assertEqual (spread 1 (sti0)) (spread-ref 1 (sti0)))
