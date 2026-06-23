; ===========================================================================
; rho-patterns -- smoke tests for strict-core rho interaction patterns.
;
; This benchmark checks rho:patterns:!recv, the Meredith--Radestock lazy
; replicated input encoding:
;
;   D(x)     = x(y).(x[y] | *y)
;   !u(v).P  = x< u(v).(D(x) | P) > | D(x)
;
; The test sends three distinct output channels to one replicated receiver.
; The receiver must handle all three messages, triggering a downstream
; three-way join that emits (rho:val all3).  The persistent listener remains
; armed in the residual, as expected; the gate checks that no deferred payload is
; stranded.
; ===========================================================================

!(import! &self rhometta)
!(import! &self rho:patterns)

(= (chan $n)
   (if (== $n 0)
       (rho:quote rho:nil)
       (rho:quote (rho:send (chan (- $n 1)) rho:nil))))

(= (count-tuple $t)
   (if (== $t ()) 0 (+ 1 (count-tuple (cdr-atom $t)))))

(= (verdict $expected $got)
   (if (== $expected $got) PASS *FAIL*))

(= (gate $tag $expected $got $desc)
   (=== $tag (verdict $expected $got) expect $expected got $got : $desc))

(= (sender $out)
   ; Name output x[y] is encoded as x<*y>.
   (rho:send (chan 1) (rho:drop $out)))

(= (listener)
   (rho:patterns:!recv (chan 2) (chan 1) $v
     (rho:send $v rho:nil)))

(= (join3)
   (rho:recv (chan 10) $a
     (rho:recv (chan 11) $b
       (rho:recv (chan 12) $c
         (rho:val all3)))))

(= (replicated-recv-net)
   (rho:par
     (listener)
     (sender (chan 10))
     (sender (chan 11))
     (sender (chan 12))
     (join3)))

; Projection helpers over the small residual shapes this benchmark emits.
(= (all3-of $r)
   (case $r
     (((rho:val all3) all3)
      ((rho:par $a $b)
       (superpose ((all3-of $a) (all3-of $b))))
      ((rho:par $a $b $c)
       (superpose ((all3-of $a) (all3-of $b) (all3-of $c))))
      ((rho:par $a $b $c $d)
       (superpose ((all3-of $a) (all3-of $b) (all3-of $c) (all3-of $d))))
      ((rho:par $a $b $c $d $e)
       (superpose ((all3-of $a) (all3-of $b) (all3-of $c) (all3-of $d)
                   (all3-of $e))))
      ($_ (empty)))))

(= (eval-payload-of $r)
   (case $r
     (((rho:eval-payload $x) (rho:eval-payload $x))
      ((rho:send $c $m)
       (superpose ((eval-payload-of $c) (eval-payload-of $m))))
      ((rho:recv $c $v $p)
       (superpose ((eval-payload-of $c) (eval-payload-of $p))))
      ((rho:drop $n) (eval-payload-of $n))
      ((rho:quote $p) (eval-payload-of $p))
      ((rho:par $a $b)
       (superpose ((eval-payload-of $a) (eval-payload-of $b))))
      ((rho:par $a $b $c)
       (superpose ((eval-payload-of $a) (eval-payload-of $b)
                   (eval-payload-of $c))))
      ((rho:par $a $b $c $d)
       (superpose ((eval-payload-of $a) (eval-payload-of $b)
                   (eval-payload-of $c) (eval-payload-of $d))))
      ((rho:par $a $b $c $d $e)
       (superpose ((eval-payload-of $a) (eval-payload-of $b)
                   (eval-payload-of $c) (eval-payload-of $d)
                   (eval-payload-of $e))))
      ($_ (empty)))))

(= (run-net)
   (rhometta:run (replicated-recv-net)))

; ===========================================================================
!(gate ACT1-all-distinct 1
   (count-tuple (collapse (all3-of (run-net))))
   "rho:patterns:!recv handles three distinct messages on one channel")

!(gate ACT2-no-stranded-eval 0
   (count-tuple (collapse (eval-payload-of (run-net))))
   "persistent residual leaves no stranded rho:eval-payload")

!(gate ACT3-strict-core-shape 0
   (count-tuple (collapse (eval-payload-of (rho:patterns:!recv (chan 2) (chan 1) $v
                                             (rho:send $v rho:nil)))))
   "the pattern expansion itself uses no rho:eval-payload or engine extension")

!("---- residual includes an expected armed listener plus (rho:val all3) ----")
!(run-net)
