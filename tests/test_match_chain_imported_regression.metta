; Imported nested-match chain regression:
; a pathmap-backed source copied through match must still expose every
; first-round ax-mp candidate through the nested match chain.

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

!(assertEqualToResult
  (collapse
    (with-space-snapshot $ss0 &ss
      (match $ss0 (: $x1 $a1)
        (match $ss0 (: $x2 $a2)
          (match &rb (: $f (-> $a1 $a2 $b))
            (: pair $x1 $x2))))))
  (((: pair ax-1 ax-1)
    (: pair ax-1 ax-2)
    (: pair ax-1 ax-3)
    (: pair ax-2 ax-1)
    (: pair ax-2 ax-2)
    (: pair ax-2 ax-3))))

!(fs-add-to-ss-nodup &rb &ss)
