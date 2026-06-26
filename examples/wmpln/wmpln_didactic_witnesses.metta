!(import! &self ../../lib/lib_wmpln)

; Executable mirrors of the Lean didactic witnesses in
; Mettapedia/Logic/PLNDidacticWitnesses.lean.
;
; These are arithmetic canaries: Lean proves the theorem surface, while this
; file checks that the same numbers are visible in the CeTTa HE-extended layer.

(= (strength-haldane $pos $neg)
   (let $total (+ $pos $neg)
        (if (== $total 0.0)
            0.0
            (/ $pos $total))))

(= (pln-confidence $weight $k)
   (/ $weight (+ $weight $k)))

(= (incoherent-two-path $r $s)
   (+ (* $r $r) (* $s $s)))

(= (coherent-two-path $r $s $cos-delta)
   (+ (incoherent-two-path $r $s)
      (* (* (* 2.0 $r) $s) $cos-delta)))

(= (pln-revision $c1 $c2)
   (/ (- (+ $c1 $c2) (* (* 2.0 $c1) $c2))
      (- 1.0 (* $c1 $c2))))

(= (exp-revision $c1 $c2)
   (- (+ $c1 $c2) (* $c1 $c2)))

(= (tanh-revision $c1 $c2)
   (/ (+ $c1 $c2) (+ 1.0 (* $c1 $c2))))

(= (complex-norm $a $b)
   (+ (* $a $a) (* $b $b)))

(= (dual-norm $a $b)
   (* $a $a))

(= (split-norm $a $b)
   (- (* $a $a) (* $b $b)))

(= (lr-to-prob $lr)
   (/ $lr (+ 1.0 $lr)))

(= (mixture $p $q $t)
   (+ (* (- 1.0 $t) $p) (* $t $q)))

; Marble bag: one red draw has strength 1 but low confidence; a large
; balanced draw has strength 1/2 and high confidence.
!(assertEqual (strength-haldane 1.0 0.0) 1.0)
!(assertEqual (pln-confidence 1.0 2.0) 0.3333333333333333)
!(assertEqual (strength-haldane 100.0 100.0) 0.5)
!(assertEqual (< 0.99 (pln-confidence 200.0 2.0)) True)
!(assertEqual
  (< (pln-confidence 1.0 2.0)
     (pln-confidence 200.0 2.0))
  True)

; Loudspeakers: coherent phase can reinforce or cancel where incoherent
; addition predicts the same weight 2.
!(assertEqual (incoherent-two-path 1.0 1.0) 2.0)
!(assertEqual (coherent-two-path 1.0 1.0 1.0) 4.0)
!(assertEqual (coherent-two-path 1.0 1.0 -1.0) 0.0)
!(assertEqual
  (== (coherent-two-path 1.0 1.0 1.0)
      (incoherent-two-path 1.0 1.0))
  False)
!(assertEqual
  (== (coherent-two-path 1.0 1.0 -1.0)
      (incoherent-two-path 1.0 1.0))
  False)

; Same displayed confidence 1/2, three different chart laws.
!(assertEqual (pln-revision 0.5 0.5) 0.6666666666666666)
!(assertEqual (exp-revision 0.5 0.5) 0.75)
!(assertEqual (tanh-revision 0.5 0.5) 0.8)
!(assertEqual
  (== (pln-revision 0.5 0.5)
      (exp-revision 0.5 0.5))
  False)
!(assertEqual
  (== (exp-revision 0.5 0.5)
      (tanh-revision 0.5 0.5))
  False)

; Goldilocks carrier: complex norm is positive-definite shaped, dual forgets
; the second coordinate, and split-complex can go negative.
!(assertEqual (complex-norm 3.0 4.0) 25.0)
!(assertEqual (dual-norm 3.0 4.0) 9.0)
!(assertEqual (split-norm 3.0 4.0) -7.0)
!(assertEqual (< (split-norm 3.0 4.0) 0.0) True)

; Pool versus chain: averaging two identical 3/4 posteriors stays 3/4,
; but chaining two likelihood-ratio-3 tests gives probability 9/10.
!(assertEqual (lr-to-prob 3.0) 0.75)
!(assertEqual (lr-to-prob 9.0) 0.9)
!(assertEqual (mixture 0.75 0.75 0.5) 0.75)
!(assertEqual
  (== (mixture 0.75 0.75 0.5)
      (lr-to-prob 9.0))
  False)
