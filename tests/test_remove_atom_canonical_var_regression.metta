; remove-atom must remove a theorem previously stored from one match when it is
; re-observed through a separate match in the same evaluation.

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $a (→ $b $a))))
!(bind! &ss (new-space))
!(assertEqualToResult
  (match &ab (: $x1 $t1)
    (let* (
      (() (add-atom &ss (: $x1 $t1)))
      (() (match &ab (: $x2 $t2) (remove-atom &ss (: $x2 $t2)))))
      (space-len &ss)))
  (0))
