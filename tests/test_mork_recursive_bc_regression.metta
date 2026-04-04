; Regression: recursive backward chaining over the PathMap-backed lane
; must preserve the same theorem as the native space on Nil's
; depth-2 propositional-calculus witness.

!(import! &self mork)

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(= (bc $rb $ab $depth (: $x $a))
   (match $ab (: $x $a) (: $x $a)))

(= (bc $rb $ab (S $k) (: ($f $x1 $x2) $b))
   (match $rb (: $f (-> $a1 $a2 $b))
     (let* (((: $x1 $a1) (bc $rb $ab $k (: $x1 $a1)))
            ((: $x2 $a2) (bc $rb $ab $k (: $x2 $a2))))
       (: ($f $x1 $x2) $b))))

!(bind! &rb-native (new-space))
!(add-atom &rb-native (: ax-mp (-> (→ $𝜑 $𝜓) $𝜑 $𝜓)))

!(bind! &ab-native (new-space))
!(add-atom &ab-native (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))
!(add-atom &ab-native (: ax-2 (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))
!(add-atom &ab-native (: ax-3 (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))))

!(bind! &rb-mork (new-space pathmap))
!(add-atom &rb-mork (: ax-mp (-> (→ $𝜑 $𝜓) $𝜑 $𝜓)))

!(bind! &ab-mork (new-space pathmap))
!(add-atom &ab-mork (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))
!(add-atom &ab-mork (: ax-2 (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))
!(add-atom &ab-mork (: ax-3 (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))))

!(assertAlphaEqualToResult
  (bc &rb-native &ab-native (fromNumber 2)
      (: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1)) $t))
  ((: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1))
      (→ (→ $p (→ $p $r)) (→ $p $r)))))

!(assertAlphaEqualToResult
  (bc &rb-mork &ab-mork (fromNumber 2)
      (: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1)) $t))
  ((: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1))
      (→ (→ $p (→ $p $r)) (→ $p $r)))))
