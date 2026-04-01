; Regression: forward-chaining depth-3 witness over `new-space mork`.
;
; The witness
;   (ax-mp (ax-mp ax-2 ax-2)
;          (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-1 ax-2)))
; should compose from the depth-2 solution space on both native and MORK
; spaces. If it fails only on MORK, the remaining bug is in imported
; nested-match candidate selection/rematch for non-epoch queries.

!(import! &self mork)

(= (seed-space-nodup $ab $ss)
   (match $ab (: $x $a)
     (add-atom-nodup $ss (: $x $a))))

(= (fs-add-to-ss-nodup $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: $x1 $a1)
       (match $ss0 (: $x2 $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (add-atom-nodup $ss (: ($f $x1 $x2) $b)))))))

(= (compose-depth3-witness $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: (ax-mp ax-2 ax-2) $a1)
       (match $ss0
         (: (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-1 ax-2)) $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (: ($f (ax-mp ax-2 ax-2)
                  (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-1 ax-2)))
              $b))))))

!(bind! &rb-native (new-space))
!(add-atom &rb-native (: ax-mp (-> (→ $𝜑 $𝜓) $𝜑 $𝜓)))

!(bind! &ab-native (new-space))
!(add-atom &ab-native (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))
!(add-atom &ab-native (: ax-2 (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))
!(add-atom &ab-native (: ax-3 (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))))

!(bind! &rb-mork (new-space mork))
!(add-atom &rb-mork (: ax-mp (-> (→ $𝜑 $𝜓) $𝜑 $𝜓)))

!(bind! &ab-mork (new-space mork))
!(add-atom &ab-mork (: ax-1 (→ $𝜑 (→ $𝜓 $𝜑))))
!(add-atom &ab-mork (: ax-2 (→ (→ $𝜑 (→ $𝜓 $𝜒)) (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))
!(add-atom &ab-mork (: ax-3 (→ (→ (¬ $𝜑) (¬ $𝜓)) (→ $𝜓 $𝜑))))

!(bind! &ss2-native (new-space))
!(seed-space-nodup &ab-native &ss2-native)
!(fs-add-to-ss-nodup &rb-native &ss2-native)
!(fs-add-to-ss-nodup &rb-native &ss2-native)

!(bind! &ss2-mork (new-space mork))
!(seed-space-nodup &ab-mork &ss2-mork)
!(fs-add-to-ss-nodup &rb-mork &ss2-mork)
!(fs-add-to-ss-nodup &rb-mork &ss2-mork)

!(assertAlphaEqualToResult
  (compose-depth3-witness &rb-native &ss2-native)
  ((: (ax-mp (ax-mp ax-2 ax-2)
             (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-1 ax-2)))
      (→ (→ (→ $𝜑 (→ $𝜓 $𝜒))
             (→ (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))
                 (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))))
         (→ (→ $𝜑 (→ $𝜓 $𝜒))
            (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))))

!(assertAlphaEqualToResult
  (compose-depth3-witness &rb-mork &ss2-mork)
  ((: (ax-mp (ax-mp ax-2 ax-2)
             (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-1 ax-2)))
      (→ (→ (→ $𝜑 (→ $𝜓 $𝜒))
             (→ (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))
                 (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒))))
         (→ (→ $𝜑 (→ $𝜓 $𝜒))
            (→ (→ $𝜑 $𝜓) (→ $𝜑 $𝜒)))))))
