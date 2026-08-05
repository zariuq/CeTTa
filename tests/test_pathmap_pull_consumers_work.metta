; Counted-PathMap pull-consumer regression.
;
; A non-ground row for an unrelated relation must not disable indexed pull
; for `row`.  The direct collapse preserves occurrence order and duplicates.
!(bind! &pull (new-space pathmap))
!(add-atom &pull (row a 1))
!(add-atom &pull (row a 1))
!(add-atom &pull (row b 2))
!(add-atom &pull (irrelevant $value))

; Keep the streamed-template witness genuinely pure.  A variable-headed
; expression is conservatively relational under the generated effect law and
; therefore belongs on the oracle path, not in this positive discriminator.
!(assertEqualToResult
  (collapse (match &pull (row $key $value) $value))
  ((1 1 2)))

; A non-ground row in the queried relation invalidates the index admission.
; The pull consumer must decline before observing a row and replay through
; the ordinary matcher, retaining both occurrences exactly.
!(add-atom &pull (fallback red any))
!(add-atom &pull (fallback $value any))
!(assertEqualToResult
  (collapse (match &pull (fallback red any) hit))
  ((hit hit)))

; Atom consumers share the same bounded cursor.  `once` and `select` stop the
; producer after the demanded prefix; collapse and fold consume the exact
; counted bag without first building a get-atoms OutcomeSet.
!(bind! &atoms (new-space pathmap))
!(add-atom &atoms alpha)
!(add-atom &atoms beta)
!(add-atom &atoms beta)
!(once (get-atoms &atoms))
!(select 2 (get-atoms &atoms))
!(collapse (get-atoms &atoms))
!(fold (get-atoms &atoms) () $acc $item
  (eval (cons-atom $item $acc)))

; The expr-row cursor does not carry stored-variable opening contexts yet.
; Such spaces must decline before cursor admission and remain on the exact
; materializing oracle rather than alpha-corrupting their atoms.
!(bind! &vars (new-space pathmap))
!(add-atom &vars (edge $x a))
!(collapse (get-atoms &vars))

; Empty is an internal no-result sentinel in the ordinary OutcomeSet path.
; A bounded pull must skip it rather than spending its demand on no result.
!(bind! &sentinel (new-space pathmap))
!(add-atom &sentinel Empty)
!(add-atom &sentinel visible)
!(once (get-atoms &sentinel))
!(select 2 (get-atoms &sentinel))
!(collapse (get-atoms &sentinel))

; `collapse` followed by `superpose` may cancel its transient tuple only for
; the admitted immutable get-atoms snapshot.  Stored computations are still
; evaluated as superpose branches, and an enclosing once remains bounded.
!(bind! &superposed (new-space pathmap))
!(add-atom &superposed (+ 1 2))
!(add-atom &superposed (+ 3 4))
!(assertEqual
  (let $items (collapse (get-atoms &superposed))
    (superpose $items))
  (superpose (3 7)))
!(assertEqualToResult
  (size
    (collapse
      (once
        (let $items (collapse (get-atoms &superposed))
          (superpose $items)))))
  (1))

; Preserve collapse's producer-error law: any ordinary row suppresses stored
; producer errors, while an all-error bag retains every error occurrence.
!(bind! &superposed-mixed (new-space pathmap))
!(add-atom &superposed-mixed (Error source hidden))
!(add-atom &superposed-mixed (+ 8 1))
!(assertEqualToResult
  (let $items (collapse (get-atoms &superposed-mixed))
    (superpose $items))
  (9))

!(bind! &superposed-errors (new-space pathmap))
!(add-atom &superposed-errors (Error source one))
!(add-atom &superposed-errors (Error source two))
!(assertEqual
  (let $items (collapse (get-atoms &superposed-errors))
    (superpose $items))
  (superpose ((Error source one) (Error source two))))

; Demand must bound physical readback.  Populate a much larger exact space,
; verify its latent cardinality, then observe one atom.  The runtime gate
; requires this consumer to visit one row rather than materializing all 4096.
!(import! &self list)
!(bind! &bulk (new-space pathmap))
!(let $ignored
  (collapse
    (let $xs (eval (list:range 4096))
      (let $i (superpose $xs)
        (add-atom &bulk (friend sam $i)))))
  ())
!(assertEqualToResult (size &bulk) (4096))
!(assertEqualToResult
  (size (collapse (once (get-atoms &bulk))))
  (1))

; A nested match belongs to the relation-level conjunction planner.  The
; row-pull accelerator must decline before opening its outer cursor rather
; than issuing one inner query per outer row.
!(bind! &joined (new-space pathmap))
!(add-atom &joined (left n0 k0))
!(add-atom &joined (right k0 v0))
!(add-atom &joined (left n1 k1))
!(add-atom &joined (right k1 v1))
!(assertEqualToResult
  (collapse
    (match &joined (left $x $k)
      (match &joined (right $k $y) ($x $y))))
  (((n0 v0) (n1 v1))))
