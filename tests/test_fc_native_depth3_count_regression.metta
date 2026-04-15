; Regression: Nil's FC depth 3 on native spaces must give 3421 results.
;
; This intentionally follows the snapshot/add accumulation lane used by the
; byte-backed FC witness. An older collapse-based accumulation probe was not a
; stable correctness witness for CeTTa/HE's current evaluation discipline.

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

!(bind! &rb (new-space))
!(add-atom &rb (: ax-mp (-> (→ $p $q) $p $q)))

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $a (→ $b $a))))
!(add-atom &ab (: ax-2 (→ (→ $a (→ $b $c)) (→ (→ $a $b) (→ $a $c)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $a) (¬ $b)) (→ $b $a))))

!(bind! &ss (new-space))
!(match &ab (: $x $a) (add-atom &ss (: $x $a)))
!(reset-runtime-stats!)
!(fs-add-to-ss &rb &ss)
!(fs-add-to-ss &rb &ss)
!(fs-add-to-ss &rb &ss)
!(assertEqualToResult (count-atoms &ss) (3421))
!(bind! &all-results (get-atoms &ss))
!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats
         (runtime-counter outcome-variant-slot-materialize $n)
         (== $n 0))
  (True))
!(assertEqualToResult
  (match &stats
         (runtime-counter outcome-variant-materialize-top-level $n)
         (== $n 0))
  (True))
