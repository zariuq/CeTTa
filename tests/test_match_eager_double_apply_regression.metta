; Regression: eager match evaluation must agree with the binding-preserving
; collapse-bind/superpose-bind roundtrip on nested match+let witnesses.

!(bind! &rb (new-space))
!(add-atom &rb (: ax-mp (-> (→ $𝜑 $𝜓) $𝜑 $𝜓)))

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))

(= (manual-match-witness)
  (match &rb (: ax-mp (-> $a1 $a2 $b))
    (let (: (ax-mp ax-1 ax-1) $a1)
      (match &rb (: ax-mp (-> $c1 $c2 $a1))
        (let (: ax-1 $c1) (match &ab (: ax-1 $c1) (: ax-1 $c1))
          (let (: ax-1 $c2) (match &ab (: ax-1 $c2) (: ax-1 $c2))
            (: (ax-mp ax-1 ax-1) $a1))))
      (let (: ax-1 $a2) (match &ab (: ax-1 $a2) (: ax-1 $a2))
        (: (ax-mp (ax-mp ax-1 ax-1) ax-1) $b)))))

(= (manual-match-roundtrip)
  (let $r (collapse-bind (manual-match-witness))
    (superpose-bind $r)))

!(assertAlphaEqual
  (manual-match-roundtrip)
  (: (ax-mp (ax-mp ax-1 ax-1) ax-1) (→ $u (→ $v $u))))

!(assertAlphaEqual
  (manual-match-witness)
  (: (ax-mp (ax-mp ax-1 ax-1) ax-1) (→ $u (→ $v $u))))

!(assertAlphaEqual
  (manual-match-witness)
  (manual-match-roundtrip))
