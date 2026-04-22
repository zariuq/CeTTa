; add-atom-nodup over a PathMap-backed space must deduplicate an alpha-equivalent
; theorem re-observed through a separate match, even though the projected
; backend atoms use synthetic stable variable spellings.

!(import! &self mork)

!(bind! &ab (new-space pathmap))
!(add-atom &ab (: ax-1 (→ $a (→ $b $a))))

!(bind! &ss (new-space pathmap))
!(assertEqualToResult
  (match &ab (: $x1 $t1)
    (match &ab (: $x2 $t2)
      (let* (
        (() (add-atom &ss (: $x1 $t1)))
        (() (add-atom-nodup &ss (: $x2 $t2))))
        (space-len &ss))))
  (1))
