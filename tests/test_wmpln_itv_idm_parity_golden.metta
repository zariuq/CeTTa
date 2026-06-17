; Golden parity checks for the Lean ITV/IDM surface.
; These mirror theorem-level arithmetic plus the typed bridge/provenance
; discipline, without claiming a full credal-set runtime in CeTTa.

(: itv-lower (-> Atom Number))
(= (itv-lower (itv $l $u $c)) $l)

(: itv-upper (-> Atom Number))
(= (itv-upper (itv $l $u $c)) $u)

(: itv-credibility (-> Atom Number))
(= (itv-credibility (itv $l $u $c)) $c)

(: itv-width (-> Atom Number))
(= (itv-width (itv $l $u $c)) (- $u $l))

(: itv-midpoint (-> Atom Number))
(= (itv-midpoint (itv $l $u $c)) (/ (+ $l $u) 2.0))

(: idm-lower (-> Number Number Number Number))
(= (idm-lower $count $total $s) (/ $count (+ $total $s)))

(: idm-upper (-> Number Number Number Number))
(= (idm-upper $count $total $s) (/ (+ $count $s) (+ $total $s)))

(: idm-width (-> Number Number Number))
(= (idm-width $total $s) (/ $s (+ $total $s)))

(: idm-credibility (-> Number Number Number))
(= (idm-credibility $total $s) (/ $total (+ $total $s)))

(: idm-itv (-> Number Number Number Atom))
(= (idm-itv $count $total $s)
   (itv (idm-lower $count $total $s)
        (idm-upper $count $total $s)
        (idm-credibility $total $s)))

(: pln-odds-decode (-> Number Number))
(= (pln-odds-decode $c) (/ $c (- 1.0 $c)))

(: pln-odds-encode (-> Number Number))
(= (pln-odds-encode $w) (/ $w (+ $w 1.0)))

(: reserve-half-decode (-> Number Number))
(= (reserve-half-decode $c) (/ $c (- 1.0 (* 2.0 $c))))

(: itv-conjunction (-> Atom Atom Atom))
(= (itv-conjunction (itv $l1 $u1 $c1) (itv $l2 $u2 $c2))
   (itv (* $l1 $l2)
        (* $u1 $u2)
        (pln-odds-encode (* (pln-odds-decode $c1)
                            (pln-odds-decode $c2)))))

(: typed-semantics (-> Atom Atom))
(= (typed-semantics (typed $sem $itv)) $sem)

(: typed-value (-> Atom Atom))
(= (typed-value (typed $sem $itv)) $itv)

(: typed-forget-generic (-> Atom Atom))
(= (typed-forget-generic (typed $sem $itv)) (typed generic $itv))

(: typed-conjunction-same (-> Atom Atom Atom))
(= (typed-conjunction-same (typed $sem $x) (typed $sem $y))
   (let* (($z (itv-conjunction $x $y)))
     (typed derived-same-conjunction $z)))

(: typed-conjunction-via-generic-bridge (-> Atom Atom Atom))
(= (typed-conjunction-via-generic-bridge (typed $sem1 $x) (typed $sem2 $y))
   (let* (($z (itv-conjunction $x $y)))
     (typed derived-generic-conjunction $z)))

(: typed-conjunction-same-semantics (-> Atom Atom Atom))
(= (typed-conjunction-same-semantics (typed $sem $x) (typed $sem $y))
   derived-same-conjunction)

(: typed-conjunction-same-lower (-> Atom Atom Number))
(= (typed-conjunction-same-lower (typed $sem (itv $l1 $u1 $c1))
                                 (typed $sem (itv $l2 $u2 $c2)))
   (* $l1 $l2))

(: typed-conjunction-via-generic-bridge-semantics (-> Atom Atom Atom))
(= (typed-conjunction-via-generic-bridge-semantics
     (typed $sem1 $x) (typed $sem2 $y))
   derived-generic-conjunction)

(: typed-conjunction-via-generic-bridge-credibility (-> Atom Atom Number))
(= (typed-conjunction-via-generic-bridge-credibility
     (typed $sem1 (itv $l1 $u1 $c1))
     (typed $sem2 (itv $l2 $u2 $c2)))
   (pln-odds-encode (* (pln-odds-decode $c1)
                       (pln-odds-decode $c2))))

(: dirichlet-empirical-mean (-> Number Number Number))
(= (dirichlet-empirical-mean $count $total) (/ $count $total))

(: dirichlet-prior-concentration (-> Number Number Number))
(= (dirichlet-prior-concentration $categories $prior)
   (* $categories $prior))

(: dirichlet-blend-weight (-> Number Number Number Number))
(= (dirichlet-blend-weight $total $categories $prior)
   (/ $total (+ $total (dirichlet-prior-concentration $categories $prior))))

(: dirichlet-symmetric-prior-mean (-> Number Number))
(= (dirichlet-symmetric-prior-mean $categories) (/ 1.0 $categories))

(: dirichlet-posterior-mean (-> Number Number Number Number Number))
(= (dirichlet-posterior-mean $count $total $categories $prior)
   (/ (+ $prior $count)
      (+ (dirichlet-prior-concentration $categories $prior) $total)))

(: dirichlet-blended-mean (-> Number Number Number Number Number))
(= (dirichlet-blended-mean $count $total $categories $prior)
   (let* (($w (dirichlet-blend-weight $total $categories $prior))
          ($emp (dirichlet-empirical-mean $count $total))
          ($base (dirichlet-symmetric-prior-mean $categories)))
     (+ (* $w $emp) (* (- 1.0 $w) $base))))

(: categorical-revised-mean (-> Number Number Number Number Number))
(= (categorical-revised-mean $count1 $total1 $count2 $total2)
   (/ (+ $count1 $count2) (+ $total1 $total2)))

(: categorical-revised-decoded-count (-> Number Number Number Number Number))
(= (categorical-revised-decoded-count $count1 $total1 $count2 $total2)
   (* (categorical-revised-mean $count1 $total1 $count2 $total2)
      (+ $total1 $total2)))

!(assertEqual (itv-lower (itv 0.0 1.0 0.5)) 0.0)
!(assertEqual (itv-midpoint (itv 0.0 1.0 0.5)) 0.5)
!(assertEqual (itv-upper (itv 0.0 1.0 0.5)) 1.0)
!(assertEqual (itv-width (itv 0.0 1.0 0.5)) 1.0)

; Three-category IDM example: counts (2,3,5), total 10, s=2, category 0.
!(assertEqual (idm-lower 2.0 10.0 2.0) 0.16666666666666666)
!(assertEqual (idm-upper 2.0 10.0 2.0) 0.3333333333333333)
!(assertEqual (idm-width 10.0 2.0) 0.16666666666666666)
!(assertEqual (+ (idm-width 10.0 2.0) (idm-credibility 10.0 2.0)) 1.0)
!(assertEqual (let* (($x (idm-itv 2.0 10.0 2.0))) (itv-lower $x))
              0.16666666666666666)
!(assertEqual (let* (($x (idm-itv 2.0 10.0 2.0))) (itv-upper $x))
              0.3333333333333333)
!(assertEqual (let* (($x (idm-itv 2.0 10.0 2.0))) (itv-width $x))
              0.16666666666666666)
!(assertEqual (let* (($x (idm-itv 2.0 10.0 2.0)))
                (+ (itv-width $x) (itv-credibility $x)))
              1.0)

; Same displayed confidence, different coordinate provenance, different weight.
!(assertEqual (pln-odds-decode 0.25) 0.3333333333333333)
!(assertEqual (reserve-half-decode 0.25) 0.5)

; Symmetric Dirichlet example: counts (2,3,5), total 10, three categories,
; prior 2 per category.  The posterior mean for category 0 is the blend of
; empirical mean 2/10 with prior mean 1/3, weighted by 10/(10+3*2).
!(assertEqual (dirichlet-prior-concentration 3.0 2.0) 6.0)
!(assertEqual (dirichlet-blend-weight 10.0 3.0 2.0) 0.625)
!(assertEqual (dirichlet-posterior-mean 2.0 10.0 3.0 2.0) 0.25)
!(assertEqual (dirichlet-blended-mean 2.0 10.0 3.0 2.0) 0.25)

; Typed categorical revision parity: category counts 2/10 and 1/2 revise to
; count 3 over total 12, so decoding the revised mean recovers the added count.
!(assertEqual (categorical-revised-mean 2.0 10.0 1.0 2.0) 0.25)
!(assertEqual (categorical-revised-decoded-count 2.0 10.0 1.0 2.0) 3.0)

; Typed bridge/provenance parity: same raw ITV can be carried under different
; tags; forgetting to generic preserves the raw value; cross-semantics
; conjunction goes through the explicit generic bridge.
!(assertEqual (typed-semantics (typed walley (itv 0.2 0.4 0.5))) walley)
!(assertEqual (typed-value (typed walley (itv 0.2 0.4 0.5))) (itv 0.2 0.4 0.5))
!(assertEqual (typed-forget-generic (typed walley (itv 0.2 0.4 0.5)))
              (typed generic (itv 0.2 0.4 0.5)))
!(assertEqual (typed-conjunction-same-semantics
                (typed generic (itv 0.2 0.4 0.5))
                (typed generic (itv 0.5 0.5 0.5)))
              derived-same-conjunction)
!(assertEqual (typed-conjunction-same-lower
                (typed generic (itv 0.2 0.4 0.5))
                (typed generic (itv 0.5 0.5 0.5)))
              0.1)
!(assertEqual (typed-conjunction-via-generic-bridge-semantics
                (typed walley (itv 0.2 0.4 0.5))
                (typed bayes (itv 0.5 0.5 0.5)))
              derived-generic-conjunction)
