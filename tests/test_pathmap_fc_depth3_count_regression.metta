; Regression: full depth-3 forward-chaining outcome count on a PathMap-backed
; space should match the long-standing native depth-3 witness surface.
;
; This keeps the first serious FC lane separate from the BC/WAM-style tests so
; we can measure and reason about the divergence honestly.

(= (depth) 3)

(: Nat Type)
(: Z Nat)
(: S (-> Nat Nat))
(: fromNumber (-> Number Nat))
(= (fromNumber $n) (if (<= $n 0) Z (S (fromNumber (- $n 1)))))

(= (fs2 $rb (: $x1 $a1) (: $x2 $a2))
   (match $rb (: $f (-> $a1 $a2 $b)) (: ($f $x1 $x2) $b)))

(= (fs $rb $ss)
   (fs2 $rb
        (match $ss (: $x1 $a1) (: $x1 $a1))
        (match $ss (: $x2 $a2) (: $x2 $a2))))

(= (fs-add-to-ss $rb $ss)
   (with-space-snapshot $ss0 $ss
     (match $ss0 (: $x1 $a1)
       (match $ss0 (: $x2 $a2)
         (match $rb (: $f (-> $a1 $a2 $b))
           (add-atom $ss (: ($f $x1 $x2) $b)))))))

!(bind! &rb (new-space pathmap))
!(add-atom &rb (: ax-mp (-> (→ $p $q) $p $q)))

!(bind! &ab (new-space pathmap))
!(add-atom &ab (: ax-1 (→ $p (→ $q $p))))
!(add-atom &ab (: ax-2 (→ (→ $p (→ $q $r)) (→ (→ $p $q) (→ $p $r)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $p) (¬ $q)) (→ $q $p))))

!(bind! &ss (new-space pathmap))
!(match &ab (: $x $a) (add-atom &ss (: $x $a)))

!(if (<= 1 (depth)) (fs-add-to-ss &rb &ss) (empty))
!(if (<= 2 (depth)) (fs-add-to-ss &rb &ss) (empty))
!(if (<= 3 (depth)) (fs-add-to-ss &rb &ss) (empty))

!(assertEqualToResult (count-atoms &ss) (3421))
!(bind! &all-results (get-atoms &ss))
!(assertEqualToResult
  (match &ss
    (: (ax-mp (ax-mp ax-2 ax-2)
              (ax-mp (ax-mp ax-2 ax-2) (ax-mp ax-1 ax-2)))
       $t)
    witness)
  (witness witness))
