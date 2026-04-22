; Regression: forward chaining over the PathMap-backed lane must preserve the same
; final solution-space counts as the native space on Nil Geisweiller's
; propositional-calculus benchmark shapes.

!(import! &self mork)

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))

(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(= (seed-space $ab $ss)
   (match $ab (: $x $a)
     (add-atom $ss (: $x $a))))

(= (seed-space-nodup $ab $ss)
   (match $ab (: $x $a)
     (add-atom-nodup $ss (: $x $a))))

(= (fs-add-to-ss $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: $x1 $a1)
       (match $ss0 (: $x2 $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (add-atom $ss (: ($f $x1 $x2) $b)))))))

(= (fs-add-to-ss-nodup $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: $x1 $a1)
       (match $ss0 (: $x2 $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (add-atom-nodup $ss (: ($f $x1 $x2) $b)))))))

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

; Depth 2 duplicate lane
!(bind! &ss2-native (new-space))
!(seed-space &ab-native &ss2-native)
!(fs-add-to-ss &rb-native &ss2-native)
!(fs-add-to-ss &rb-native &ss2-native)

!(bind! &ss2-mork (new-space pathmap))
!(seed-space &ab-mork &ss2-mork)
!(fs-add-to-ss &rb-mork &ss2-mork)
!(fs-add-to-ss &rb-mork &ss2-mork)

!(assertEqual (count-atoms &ss2-native) 72)
!(assertEqual (count-atoms &ss2-mork) 72)
!(assertEqual (count-atoms &ss2-native) (count-atoms &ss2-mork))

; Depth 3 duplicate lane
!(bind! &ss3-native (new-space))
!(seed-space &ab-native &ss3-native)
!(fs-add-to-ss &rb-native &ss3-native)
!(fs-add-to-ss &rb-native &ss3-native)
!(fs-add-to-ss &rb-native &ss3-native)

!(bind! &ss3-mork (new-space pathmap))
!(seed-space &ab-mork &ss3-mork)
!(fs-add-to-ss &rb-mork &ss3-mork)
!(fs-add-to-ss &rb-mork &ss3-mork)
!(fs-add-to-ss &rb-mork &ss3-mork)

!(println! (fc3-dup-native (count-atoms &ss3-native)))
!(println! (fc3-dup-mork (count-atoms &ss3-mork)))

; Depth 2 nodup lane
!(bind! &ss2n-native (new-space))
!(seed-space-nodup &ab-native &ss2n-native)
!(fs-add-to-ss-nodup &rb-native &ss2n-native)
!(fs-add-to-ss-nodup &rb-native &ss2n-native)

!(bind! &ss2n-mork (new-space pathmap))
!(seed-space-nodup &ab-mork &ss2n-mork)
!(fs-add-to-ss-nodup &rb-mork &ss2n-mork)
!(fs-add-to-ss-nodup &rb-mork &ss2n-mork)

!(assertEqual (count-atoms &ss2n-native) 66)
!(assertEqual (count-atoms &ss2n-mork) 66)
!(assertEqual (count-atoms &ss2n-native) (count-atoms &ss2n-mork))

; Depth 3 nodup lane
!(bind! &ss3n-native (new-space))
!(seed-space-nodup &ab-native &ss3n-native)
!(fs-add-to-ss-nodup &rb-native &ss3n-native)
!(fs-add-to-ss-nodup &rb-native &ss3n-native)
!(fs-add-to-ss-nodup &rb-native &ss3n-native)

!(bind! &ss3n-mork (new-space pathmap))
!(seed-space-nodup &ab-mork &ss3n-mork)
!(fs-add-to-ss-nodup &rb-mork &ss3n-mork)
!(fs-add-to-ss-nodup &rb-mork &ss3n-mork)
!(fs-add-to-ss-nodup &rb-mork &ss3n-mork)

!(println! (fc3-nodup-native (count-atoms &ss3n-native)))
!(println! (fc3-nodup-mork (count-atoms &ss3n-mork)))
!(let* (($native-support (unique-atom (collapse (get-atoms &ss3n-native))))
         ($mork-support (unique-atom (collapse (get-atoms &ss3n-mork)))))
   (println! (fc3-support-native-count (size-atom $native-support))))
!(let* (($native-support (unique-atom (collapse (get-atoms &ss3n-native))))
         ($mork-support (unique-atom (collapse (get-atoms &ss3n-mork)))))
   (println! (fc3-support-mork-count (size-atom $mork-support))))
!(let* (($native-support (unique-atom (collapse (get-atoms &ss3n-native))))
         ($mork-support (unique-atom (collapse (get-atoms &ss3n-mork)))))
   (println! (fc3-support-native-minus-mork-count
              (size-atom (subtraction-atom $native-support $mork-support)))))
!(let* (($native-support (unique-atom (collapse (get-atoms &ss3n-native))))
         ($mork-support (unique-atom (collapse (get-atoms &ss3n-mork)))))
   (println! (fc3-support-mork-minus-native-count
              (size-atom (subtraction-atom $mork-support $native-support)))))
!(let* (($native-support (unique-atom (collapse (get-atoms &ss3n-native))))
         ($mork-support (unique-atom (collapse (get-atoms &ss3n-mork)))))
   (println! (fc3-support-native-minus-mork
              (subtraction-atom $native-support $mork-support))))
!(let* (($native-support (unique-atom (collapse (get-atoms &ss3n-native))))
         ($mork-support (unique-atom (collapse (get-atoms &ss3n-mork)))))
   (println! (fc3-support-mork-minus-native
              (subtraction-atom $mork-support $native-support))))
!(assertEqual (count-atoms &ss3n-native) (count-atoms &ss3n-mork))
!(assertEqual (count-atoms &ss3-native) (count-atoms &ss3-mork))
