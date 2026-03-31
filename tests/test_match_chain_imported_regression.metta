; Imported nested-match chain regression:
; the optimized 3-step match chain must agree with ordinary recursive match
; semantics on this forward-chaining pattern.

!(bind! &rb (new-space))
!(add-atom &rb (: ax-mp (-> (→ $p $q) $p $q)))

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $p (→ $q $p))))
!(add-atom &ab (: ax-2 (→ (→ $p (→ $q $r)) (→ (→ $p $q) (→ $p $r)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $p) (¬ $q)) (→ $q $p))))

(= (fs-add-to-ss-nodup $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: $x1 $a1)
       (match $ss0 (: $x2 $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (add-atom-nodup $ss (: ($f $x1 $x2) $b)))))))

!(bind! &ss (new-space))
!(match &ab (: $x $a) (add-atom-nodup &ss (: $x $a)))
!(fs-add-to-ss-nodup &rb &ss)

!(assertAlphaEqualToResult
  (with-space-snapshot $ss0 &ss
    (match $ss0 (: (ax-mp ax-2 ax-2) $a1)
      (match $ss0 (: (ax-mp ax-2 ax-1) $a2)
        (match &rb (: $f (-> $a1 $a2 $b))
          (: ($f (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1)) $b)))))
  ((: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-2 ax-1))
      (→ (→ $p (→ $p $r)) (→ $p $r)))))
