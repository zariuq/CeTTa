!(import! &self rhometta)
!(import! &self lib_pln)

(= (eval:data)
   (rho:par
     (rho:send (rho:quote rho:nil) (rhometta:eval (+ 1 2)))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

!(add-atom &self (foo 1))
!(add-atom &self (foo 2))

(= (eval:kb)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval (match &self (foo $x) (got $x))))
     (rho:recv (rho:quote rho:nil) $x (rho:drop $x))))

(= (eval:kb-farm)
   (rho:par
     (rho:send (rho:quote rho:nil)
       (rhometta:eval (match &self (foo $x) (got-a $x))))
     (rho:recv (rho:quote rho:nil) $x (rho:val $x))
     (rho:send
       (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
       (rhometta:eval (match &self (foo $x) (got-b $x))))
     (rho:recv
       (rho:quote (rho:send (rho:quote rho:nil) rho:nil))
       $y
       (rho:val $y))))

(= (mix:chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (mix:chan (- $n 1)) rho:nil))))

(= (mix:quiet-cell $n $tag)
   (rho:par
     (rho:send (mix:chan $n) (rhometta:eval (+ $n 1)))
     (rho:recv (mix:chan $n) $x (rho:val $tag))))

(= (mix:contention-only)
   (rho:par
     (rho:send (mix:chan 7) (rhometta:eval (+ 1 2)))
     (rho:recv (mix:chan 7) $x (rho:val left))
     (rho:recv (mix:chan 7) $x (rho:val right))))

(= (mix:partial-contention)
   (rho:par
     (mix:quiet-cell 1 keep-a)
     (mix:quiet-cell 2 keep-b)
     (rho:send (mix:chan 7) (rhometta:eval (+ 1 2)))
     (rho:recv (mix:chan 7) $x (rho:val left))
     (rho:recv (mix:chan 7) $x (rho:val right))))

(= (mix:partial-nonquiet)
   (rho:par
     (mix:quiet-cell 1 keep-a)
     (mix:quiet-cell 2 keep-b)
     (rho:send (mix:chan 3) (rhometta:eval (+ 1 2)))
     (rho:recv (mix:chan 3) $x
       (rho:send (mix:chan 4) rho:nil))))

(= (mix:partial-unsafe)
   (rho:par
     (mix:quiet-cell 1 keep-a)
     (mix:quiet-cell 2 keep-b)
     (rho:send (mix:chan 5) (rhometta:eval (new-space)))
     (rho:recv (mix:chan 5) $x (rho:val $x))))

(= (demo:chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (demo:chan (- $n 1)) rho:nil))))

(demo:link-tv 0 1 (stv 0.8 0.9)) (demo:link-tv 0 2 (stv 0.7 0.9))
(demo:link-tv 1 1 (stv 0.9 0.8)) (demo:link-tv 1 2 (stv 0.6 0.8))

(= (demo:link-tv-of $i $k) (match &self (demo:link-tv $i $k $t) $t))
(= (demo:deduce-chain $i $k $acc)
   (if (== $k 0)
       (conclusion (job $i) $acc)
       (demo:deduce-chain $i (- $k 1)
         (Truth_Deduction (stv 0.5 0.9) (stv 0.5 0.9) (stv 0.5 0.9)
                          $acc (demo:link-tv-of $i $k)))))
(= (demo:deduce-job $i $len)
   (demo:deduce-chain $i (- $len 1) (demo:link-tv-of $i $len)))
(= (demo:cell $i $len)
   (rho:par
     (rho:send (demo:chan $i) (rhometta:eval (demo:deduce-job $i $len)))
     (rho:recv (demo:chan $i) $y (rho:drop $y))))
(= (demo:farm)
   (rho:par (demo:cell 0 2) (demo:cell 1 2)))

(ecan:hebbian a b 0.6) (ecan:hebbian a c 0.4)
(ecan:hebbian b d 0.5) (ecan:hebbian c d 0.5)
(ecan:hebbian d a 0.3) (ecan:hebbian d e 0.7)
(ecan:hebbian e f 1.0)

(= (ecan:max-spread) 0.5)

(= (ecan:chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (ecan:chan (- $n 1)) rho:nil))))

(= (ecan:sum-tuple $xs)
   (if (== $xs ()) 0.0 (+ (car-atom $xs) (ecan:sum-tuple (cdr-atom $xs)))))
(= (ecan:sti-of $x $vec)
   (if (== $vec ())
       0.0
       (let ($y $v) (car-atom $vec)
            (if (== $y $x) $v (ecan:sti-of $x (cdr-atom $vec))))))
(= (ecan:out-total $y)
   (ecan:sum-tuple (collapse (match &self (ecan:hebbian $y $z $w) $w))))
(= (ecan:inflow $x $vec)
   (ecan:sum-tuple (collapse
     (match &self (ecan:hebbian $y $x $w)
       (* (* (ecan:max-spread) (ecan:sti-of $y $vec)) (/ $w (ecan:out-total $y)))))))
(= (ecan:keep $x $vec)
   (if (> (ecan:out-total $x) 0.0)
       (* (- 1.0 (ecan:max-spread)) (ecan:sti-of $x $vec))
       (ecan:sti-of $x $vec)))
(= (ecan:new-sti $x $vec) ($x (+ (ecan:keep $x $vec) (ecan:inflow $x $vec))))
(= (ecan:cell $i $x $vec)
   (rho:par
     (rho:send (ecan:chan $i) (rhometta:eval (ecan:new-sti $x $vec)))
     (rho:recv (ecan:chan $i) $y (rho:drop $y))))
(= (ecan:round $vec)
   (rho:par (ecan:cell 1 a $vec) (ecan:cell 2 b $vec) (ecan:cell 3 c $vec)
            (ecan:cell 4 d $vec) (ecan:cell 5 e $vec) (ecan:cell 6 f $vec)))
(= (ecan:extract-vec (rho:par (rho:val $p1) (rho:val $p2) (rho:val $p3)
                              (rho:val $p4) (rho:val $p5) (rho:val $p6)))
   ($p1 $p2 $p3 $p4 $p5 $p6))
(= (ecan:round-ref $vec)
   ((ecan:new-sti a $vec) (ecan:new-sti b $vec) (ecan:new-sti c $vec)
    (ecan:new-sti d $vec) (ecan:new-sti e $vec) (ecan:new-sti f $vec)))
(= (ecan:sti0) ((a 60.0) (b 10.0) (c 10.0) (d 10.0) (e 5.0) (f 5.0)))

!(pragma! num-threads 2)
!(reset-runtime-stats!)

!(assertEqualToResult
  (collapse (rhometta:run (eval:kb)))
  (((rho:val (got 1)) (rho:val (got 2)))))

!(bind! &stats (runtime-stats!))

!(assertEqualToResult
  (match &stats
    (runtime-counter query-equations $n)
    (> $n 0))
  (True))

!(assertEqualToResult
  (match &stats
    (runtime-counter query-equation-candidates $n)
    (> $n 0))
  (True))

!(reset-runtime-stats!)
!(bind! &macro-run
   (collapse (rhometta:run (eval:kb-farm))))
!(bind! &macro-stats (runtime-stats!))
!(assertEqualToResult
  (match &macro-stats
    (runtime-counter rho-quiet-macro-applied $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &macro-stats
    (runtime-counter rho-quiet-macro-full-fire $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &macro-stats
    (runtime-counter rho-quiet-macro-partial-fire $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &macro-stats
    (runtime-counter rho-quiet-macro-bail-exact $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &macro-stats
    (runtime-counter rho-quiet-macro-fallback-contention $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &macro-stats
    (runtime-counter rho-quiet-macro-fallback-nonquiet $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &macro-stats
    (runtime-counter rho-quiet-macro-fallback-unsafe-payload $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &macro-stats
    (runtime-counter rho-quiet-macro-fallback-child-cap $n)
    (== $n 0))
  (True))

!(reset-runtime-stats!)
!(assertEqual
  (collapse (rhometta:run (demo:farm)))
  (collapse
    (rho:par
      (rho:val (demo:deduce-job 0 2))
      (rho:val (demo:deduce-job 1 2)))))
!(bind! &demo-macro-stats (runtime-stats!))
!(assertEqualToResult
  (match &demo-macro-stats
    (runtime-counter rho-quiet-macro-applied $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &demo-macro-stats
    (runtime-counter rho-quiet-macro-full-fire $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &demo-macro-stats
    (runtime-counter rho-quiet-macro-partial-fire $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &demo-macro-stats
    (runtime-counter rho-quiet-macro-bail-exact $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &demo-macro-stats
    (runtime-counter rho-quiet-macro-fallback-contention $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &demo-macro-stats
    (runtime-counter rho-quiet-macro-fallback-nonquiet $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &demo-macro-stats
    (runtime-counter rho-quiet-macro-fallback-unsafe-payload $n)
    (== $n 0))
  (True))

!(reset-runtime-stats!)
!(assertEqual
  (ecan:extract-vec (rhometta:run (ecan:round (ecan:sti0))))
  (ecan:round-ref (ecan:sti0)))
!(bind! &ecan-macro-stats (runtime-stats!))
!(assertEqualToResult
  (match &ecan-macro-stats
    (runtime-counter rho-quiet-macro-applied $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &ecan-macro-stats
    (runtime-counter rho-quiet-macro-full-fire $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &ecan-macro-stats
    (runtime-counter rho-quiet-macro-partial-fire $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &ecan-macro-stats
    (runtime-counter rho-quiet-macro-bail-exact $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &ecan-macro-stats
    (runtime-counter rho-quiet-macro-fallback-contention $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &ecan-macro-stats
    (runtime-counter rho-quiet-macro-fallback-nonquiet $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &ecan-macro-stats
    (runtime-counter rho-quiet-macro-fallback-unsafe-payload $n)
    (== $n 0))
  (True))

!(reset-runtime-stats!)
!(assertEqual (TupleCount (collapse (rhometta:run (mix:contention-only)))) 2)
!(bind! &contention-stats (runtime-stats!))
!(assertEqualToResult
  (match &contention-stats
    (runtime-counter rho-quiet-macro-applied $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &contention-stats
    (runtime-counter rho-quiet-macro-partial-fire $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &contention-stats
    (runtime-counter rho-quiet-macro-bail-exact $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &contention-stats
    (runtime-counter rho-quiet-macro-fallback-contention $n)
    (> $n 0))
  (True))

!(reset-runtime-stats!)
!(assertEqual (TupleCount (collapse (rhometta:run (mix:partial-contention)))) 2)
!(bind! &partial-contention-stats (runtime-stats!))
!(assertEqualToResult
  (match &partial-contention-stats
    (runtime-counter rho-quiet-macro-partial-fire $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &partial-contention-stats
    (runtime-counter rho-quiet-macro-fallback-contention $n)
    (> $n 0))
  (True))

!(reset-runtime-stats!)
!(assertEqual (TupleCount (collapse (rhometta:run (mix:partial-nonquiet)))) 1)
!(bind! &partial-nonquiet-stats (runtime-stats!))
!(assertEqualToResult
  (match &partial-nonquiet-stats
    (runtime-counter rho-quiet-macro-partial-fire $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &partial-nonquiet-stats
    (runtime-counter rho-quiet-macro-fallback-nonquiet $n)
    (> $n 0))
  (True))

!(reset-runtime-stats!)
!(assertEqual (TupleCount (collapse (rhometta:run (mix:partial-unsafe)))) 4)
!(bind! &partial-unsafe-stats (runtime-stats!))
!(assertEqualToResult
  (match &partial-unsafe-stats
    (runtime-counter rho-quiet-macro-partial-fire $n)
    (== $n 0))
  (True))
!(assertEqualToResult
  (match &partial-unsafe-stats
    (runtime-counter rho-quiet-macro-bail-exact $n)
    (> $n 0))
  (True))
!(assertEqualToResult
  (match &partial-unsafe-stats
    (runtime-counter rho-quiet-macro-fallback-unsafe-payload $n)
    (> $n 0))
  (True))
