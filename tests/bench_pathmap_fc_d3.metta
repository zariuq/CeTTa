; PathMap-backed variant of the depth-3 propositional forward chainer.
;
; Recommended:
;   ./cetta --quiet tests/bench_pathmap_fc_d3.metta

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

!(println! "=== PathMap FC d3 ===")

!(bind! &rb (new-space pathmap))
!(add-atom &rb (: ax-mp (-> (→ $p $q) $p $q)))

!(bind! &ab (new-space pathmap))
!(add-atom &ab (: ax-1 (→ $p (→ $q $p))))
!(add-atom &ab (: ax-2 (→ (→ $p (→ $q $r)) (→ (→ $p $q) (→ $p $r)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $p) (¬ $q)) (→ $q $p))))

!(bind! &ss (new-space pathmap))
!(match &ab (: $x $a) (add-atom &ss (: $x $a)))

!(reset-runtime-stats!)
!(if (<= 1 (depth)) (fs-add-to-ss &rb &ss) (empty))
!(if (<= 2 (depth)) (fs-add-to-ss &rb &ss) (empty))
!(if (<= 3 (depth)) (fs-add-to-ss &rb &ss) (empty))

!(assertEqualToResult (count-atoms &ss) (3421))
!(bind! &all-results (get-atoms &ss))

!(bind! &stats (runtime-stats!))
!(println! (count-atoms &ss))
!(println! (match &stats (runtime-counter outcome-variant-factor-attempt $n)
                     (outcome-variant-factor-attempt $n)))
!(println! (match &stats (runtime-counter outcome-variant-factor-success $n)
                     (outcome-variant-factor-success $n)))
!(println! (match &stats (runtime-counter outcome-variant-slot-materialize $n)
                     (outcome-variant-slot-materialize $n)))
!(println! (match &stats (runtime-counter outcome-variant-slot-sink $n)
                     (outcome-variant-slot-sink $n)))
