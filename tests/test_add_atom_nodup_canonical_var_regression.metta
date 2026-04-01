; add-atom-nodup must deduplicate a theorem re-observed through a separate
; match in the same evaluation, even though the two match instances carry
; different fresh variable ids before storage canonicalization.

!(bind! &ab (new-space))
!(add-atom &ab (: ax-1 (→ $a (→ $b $a))))
!(bind! &ss (new-space))
!(assertEqualToResult
  (match &ab (: $x1 $t1)
    (match &ab (: $x2 $t2)
      (let* (
        (() (add-atom &ss (: $x1 $t1)))
        (() (add-atom-nodup &ss (: $x2 $t2))))
        (space-len &ss))))
  (1))
