; Counted-PathMap indexed-query regression.
;
; The first query has a bound non-leading column, a shared join variable, and
; duplicate facts. The second exercises repeated-variable equality. The third
; deliberately uses a nested pattern and therefore checks exact oracle
; fallback outside the indexed flat-relational fragment.

!(bind! &idx (new-space pathmap))

!(add-atom &idx (edge a b))
!(add-atom &idx (edge a b))
!(add-atom &idx (edge c b))
!(add-atom &idx (edge d e))

!(add-atom &idx (tag a red))
!(add-atom &idx (tag a red))
!(add-atom &idx (tag a red))
!(add-atom &idx (tag c red))
!(add-atom &idx (tag c red))
!(add-atom &idx (tag d blue))

!(assertEqualToResult
  (let $rows
    (collapse
      (match &idx (edge $x b)
        (match &idx (tag $x red)
          (joined $x))))
    (size-atom $rows))
  (8))

; The identical second query must reuse its alpha-canonical plan and access
; paths rather than rescanning or rebuilding the counted store.
!(assertEqualToResult
  (let $rows
    (collapse
      (match &idx (edge $left b)
        (match &idx (tag $left red)
          (joined $left))))
    (size-atom $rows))
  (8))

; Strict size over collapse is a lawful COUNT consumer. The indexed lane
; sums the product of stored factor multiplicities without reconstructing
; either the constant result or the projected binding result.
!(assertEqualToResult
  (size
    (collapse
      (match &idx (, (edge $x b) (tag $x red)) hit)))
  (8))

!(assertEqualToResult
  (size
    (collapse
      (match &idx (, (edge $x b) (tag $x red)) $x)))
  (8))

; Mutations after the index is live update the maintained access paths. They
; invalidate the plan revision, but must not rebuild or rescan the store.
!(add-atom &idx (edge e b))
!(add-atom &idx (tag e red))
!(remove-atom &idx (tag c red))
!(assertEqualToResult
  (let $rows
    (collapse
      (match &idx (edge $person b)
        (match &idx (tag $person red)
          (joined $person))))
    (size-atom $rows))
  (8))

!(add-atom &idx (pair a a))
!(add-atom &idx (pair a b))
!(assertEqualToResult
  (collapse (match &idx (pair $x $x) $x))
  ((a)))

!(add-atom &idx (typed f (arrow a b)))
!(assertEqualToResult
  (collapse (match &idx (typed $f (arrow $a $b)) $f))
  ((f)))
