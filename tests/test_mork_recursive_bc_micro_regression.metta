; Regression: localize the PathMap-backed recursive BC mismatch.
;
; A materialized depth-1 theorem space should compose the depth-2 witness on
; both native and MORK spaces. Replacing those same materialized theorems with
; the semantically equivalent `bc depth 1` helper should also compose the same
; witness. If that second form fails only on MORK, the first bad seam is
; "equation-produced intermediate theorem fed through the outer let*/match
; chain", not the flat PathMap matcher itself.

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

(= (compose-from-space $rb $ss)
   (match $rb (: $f (-> $a1 $a2 $b))
     (let* (((: (ax-mp ax-2 ax-2) $a1)
              (match $ss (: (ax-mp ax-2 ax-2) $a1)
                (: (ax-mp ax-2 ax-2) $a1)))
            ((: (ax-mp ax-2 ax-1) $a2)
              (match $ss (: (ax-mp ax-2 ax-1) $a2)
                (: (ax-mp ax-2 ax-1) $a2))))
       (: ($f (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1)) $b))))

(= (compose-from-bc1 $rb $ab)
   (match $rb (: $f (-> $a1 $a2 $b))
     (let* (((: (ax-mp ax-2 ax-2) $a1)
              (bc $rb $ab (fromNumber 1) (: (ax-mp ax-2 ax-2) $a1)))
            ((: (ax-mp ax-2 ax-1) $a2)
              (bc $rb $ab (fromNumber 1) (: (ax-mp ax-2 ax-1) $a2))))
       (: ($f (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1)) $b))))

(= (manual-rule $rb)
   (match $rb
     (: $f
        (-> (→ (→ (→ $p (→ $q $r)) (→ $p $q))
                (→ (→ $p (→ $q $r)) (→ $p $r)))
            (→ (→ $c $b) (→ $c $c))
            $out))
     (: $f $out)))

(= (outer-rule $rb $ss)
   (let* (((: (ax-mp ax-2 ax-2) $a1)
            (match $ss (: (ax-mp ax-2 ax-2) $a1)
              (: (ax-mp ax-2 ax-2) $a1)))
          ((: (ax-mp ax-2 ax-1) $a2)
            (match $ss (: (ax-mp ax-2 ax-1) $a2)
              (: (ax-mp ax-2 ax-1) $a2))))
     (match $rb (: $f (-> $a1 $a2 $b)) (: $f $b))))

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

!(bind! &d1-native (new-space))
!(add-atom &d1-native
  (: (ax-mp ax-2 ax-2)
     (→ (→ (→ $p (→ $q $r)) (→ $p $q))
        (→ (→ $p (→ $q $r)) (→ $p $r)))))
!(add-atom &d1-native
  (: (ax-mp ax-2 ax-1)
     (→ (→ $c $b) (→ $c $c))))

!(bind! &d1-mork (new-space pathmap))
!(add-atom &d1-mork
  (: (ax-mp ax-2 ax-2)
     (→ (→ (→ $p (→ $q $r)) (→ $p $q))
        (→ (→ $p (→ $q $r)) (→ $p $r)))))
!(add-atom &d1-mork
  (: (ax-mp ax-2 ax-1)
     (→ (→ $c $b) (→ $c $c))))

!(assertAlphaEqualToResult
  (manual-rule &rb-native)
  ((: ax-mp (→ (→ $q (→ $q $r)) (→ $q $r)))))

!(assertAlphaEqualToResult
  (manual-rule &rb-mork)
  ((: ax-mp (→ (→ $q (→ $q $r)) (→ $q $r)))))

!(assertAlphaEqualToResult
  (outer-rule &rb-native &d1-native)
  ((: ax-mp (→ (→ $q (→ $q $r)) (→ $q $r)))))

!(assertAlphaEqualToResult
  (outer-rule &rb-mork &d1-mork)
  ((: ax-mp (→ (→ $q (→ $q $r)) (→ $q $r)))))

!(assertAlphaEqualToResult
  (compose-from-space &rb-native &d1-native)
  ((: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1))
      (→ (→ $p (→ $p $r)) (→ $p $r)))))

!(assertAlphaEqualToResult
  (compose-from-space &rb-mork &d1-mork)
  ((: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1))
      (→ (→ $p (→ $p $r)) (→ $p $r)))))

!(assertAlphaEqualToResult
  (compose-from-bc1 &rb-native &ab-native)
  ((: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1))
      (→ (→ $p (→ $p $r)) (→ $p $r)))))

!(assertAlphaEqualToResult
  (compose-from-bc1 &rb-mork &ab-mork)
  ((: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1))
      (→ (→ $p (→ $p $r)) (→ $p $r)))))
