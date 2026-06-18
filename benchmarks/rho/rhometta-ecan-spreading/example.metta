; rhometta ECAN attention spreading (diffusion kernel of Goertzel et al.'s
; Economic Attention Networks).  One round = a confluent rhometta farm: each
; atom's new STI is computed by a deferred MeTTa payload at its own COMM.
; Left-stochastic spreading conserves total STI (the ECAN economy invariant).

!(import! &self rhometta)

; Hebbian graph (directed weights, frozen shared KB).
(hebbian a b 0.6) (hebbian a c 0.4)
(hebbian b d 0.5) (hebbian c d 0.5)
(hebbian d a 0.3) (hebbian d e 0.7)
(hebbian e f 1.0)
; f has no outgoing links: it keeps all of its STI.

(= (max-spread) 0.5)
(= (focus-threshold) 15.0)

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

; ---- ECAN one-atom update (the deferred payload) -------------------------
(= (sum-tuple $xs)
   (if (== $xs ()) 0.0 (+ (car-atom $xs) (sum-tuple (cdr-atom $xs)))))

(= (sti-of $x $vec)
   (if (== $vec ())
       0.0
       (let ($y $v) (car-atom $vec)
            (if (== $y $x) $v (sti-of $x (cdr-atom $vec))))))

(= (out-total $y)
   (sum-tuple (collapse (match &self (hebbian $y $z $w) $w))))

; Inflow into $x: each in-neighbor $y spreads max-spread of its STI,
; distributed proportionally to its outgoing Hebbian weights.
(= (inflow $x $vec)
   (sum-tuple (collapse
     (match &self (hebbian $y $x $w)
       (* (* (max-spread) (sti-of $y $vec)) (/ $w (out-total $y)))))))

; Kept STI: atoms with outgoing links keep (1 - max-spread); sinks keep all.
(= (keep $x $vec)
   (if (> (out-total $x) 0.0)
       (* (- 1.0 (max-spread)) (sti-of $x $vec))
       (sti-of $x $vec)))

(= (new-sti $x $vec) ($x (+ (keep $x $vec) (inflow $x $vec))))

; ---- one spreading round as a confluent rhometta farm --------------------
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

; MeTTa drives the rounds; rho parallelizes within each round.
(= (spread $n $vec)
   (if (== $n 0)
       $vec
       (spread (- $n 1) (extract-vec (rhometta:run (round $vec))))))

; ---- readouts -------------------------------------------------------------
(= (total-sti $vec) (sum-tuple (collapse (let ($x $v) (superpose $vec) $v))))
(= (focus $vec)
   (collapse (let ($x $v) (superpose $vec)
                  (if (>= $v (focus-threshold)) $x (empty)))))

; Stimulus at atom a.
(= (sti0) ((a 60.0) (b 10.0) (c 10.0) (d 10.0) (e 5.0) (f 5.0)))

!("initial STI (stimulus at a); attentional focus:")
!(sti0)
!(focus (sti0))

!("after 1 round of parallel spreading; focus:")
!(spread 1 (sti0))
!(focus (spread 1 (sti0)))

!("after 3 rounds; focus has flowed along the Hebbian structure:")
!(spread 3 (sti0))
!(focus (spread 3 (sti0)))

!("ECAN economy invariant: total STI conserved (initial vs after 3 rounds)")
!(total-sti (sti0))
!(total-sti (spread 3 (sti0)))
