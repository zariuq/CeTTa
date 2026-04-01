; Regression: Nil's FC depth 3 on native spaces must give 3421 results.
; PeTTa oracle: 3421. Main branch CeTTa: 3421.
; Term-sharing branch regression: 3379.
;
; This uses the exact Nil FC program, just at depth 3.
; Note: an older probe used `size-atom (collapse ...)`, which is not a reliable
; witness here under CeTTa/HE's current evaluation discipline. This test uses
; the direct space-count surface instead.

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
   (let $ns (collapse (fs $rb $ss))
     (let $nss (superpose $ns)
       (add-atom $ss $nss))))

!(bind! &rb (new-space))
!(add-atom &rb (: ax-mp (-> (→ $p $q) $p $q)))

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $a (→ $b $a))))
!(add-atom &ab (: ax-2 (→ (→ $a (→ $b $c)) (→ (→ $a $b) (→ $a $c)))))
!(add-atom &ab (: ax-3 (→ (→ (¬ $a) (¬ $b)) (→ $b $a))))

!(bind! &ss (new-space))
!(let* (
    ($seed (collapse (match &ab (: $x $a) (add-atom &ss (: $x $a)))))
    ($step1 (collapse (fs-add-to-ss &rb &ss)))
    ($step2 (collapse (fs-add-to-ss &rb &ss)))
    ($step3 (collapse (fs-add-to-ss &rb &ss)))
    (() (assertEqualToResult (count-atoms &ss) (3421)))
  )
  ())
