; Regression: counting a collapsed native-space match preserves the semantic
; distinction between strict `size` and non-strict `size-atom`.
;
; `size` is STRICT: it evaluates (collapse (match ...)) and counts the tuple.
; `size` reports the row count (512) independent of whether an implementation
; streams or materializes the tuple.
;
; `size-atom` is NON-STRICT: applied directly to (collapse (match ...)) it
; returns the ARITY of that unevaluated expression (2), matching HE.  To count a
; collapsed tuple with `size-atom` you must first evaluate the collapse via a
; let binding; then it counts the materialized tuple (512).

!(import! &self list)

!(bind! &native-count (new-space native))
!(let $ignored
  (collapse
    (let $xs (eval (list:range 512))
      (let $x (superpose $xs)
        (add-atom &native-count (friend sam $x)))))
  ())

; Strict `size` over the collapsed match = 512.
!(assertEqualToResult
  (size (collapse (match &native-count (friend $y $x) hit)))
  (512))
; A binding-dependent template is not a one-result-per-row homomorphism.
; It must preserve ordinary filtering.
!(assertEqualToResult
  (size
    (collapse
      (match &native-count
             (friend sam $x)
             (if (== $x 0) (empty) hit))))
  (511))
; Non-strict `size-atom` over the unevaluated (collapse ...) = its arity = 2.
!(assertEqualToResult
  (size-atom (collapse (match &native-count (friend $y $x) hit)))
  (2))
; Evaluate the collapse first (let), then `size-atom` counts the tuple = 512.
!(assertEqualToResult
  (let $c (collapse (match &native-count (friend $y $x) hit)) (size-atom $c))
  (512))

; A fully rigid query may consume the discrimination leaf multiplicity
; directly.  Duplicate occurrences remain observable.  Once a stored wildcard
; overlaps the same path, the aggregate must decline to ordinary matching and
; include that occurrence as well.
!(bind! &native-rigid-count (new-space native))
!(add-atom &native-rigid-count (edge a b))
!(add-atom &native-rigid-count (edge a b))
!(add-atom &native-rigid-count (edge a c))
!(assertEqualToResult
  (size (collapse (match &native-rigid-count (edge a b) hit)))
  (2))
!(add-atom &native-rigid-count (edge a $stored-wildcard))
!(assertEqualToResult
  (size (collapse (match &native-rigid-count (edge a b) hit)))
  (3))

; A count observer discards every produced binding.  The native term-view
; route may therefore match variable-bearing stored rows without decoding or
; freshening them, while preserving repeated-variable equality, duplicate
; occurrences, and conservative fallback for nested variable structure.
!(bind! &native-dead-binding-count (new-space native))
!(add-atom &native-dead-binding-count (pair $same $same))
!(add-atom &native-dead-binding-count (pair $left $right))
!(add-atom &native-dead-binding-count (pair a b))
!(add-atom &native-dead-binding-count (pair a b))
!(assertEqualToResult
  (size (collapse (match &native-dead-binding-count (pair a a) hit)))
  (2))
!(assertEqualToResult
  (size (collapse (match &native-dead-binding-count (pair a b) hit)))
  (3))
!(assertEqualToResult
  (size
    (collapse
      (match &native-dead-binding-count (pair $query-left $query-right) hit)))
  (4))
!(add-atom &native-dead-binding-count (pair (wrap $inner) tail))
!(assertEqualToResult
  (size (collapse (match &native-dead-binding-count (pair $any tail) hit)))
  (3))
; A rigid query against nested stored variables is outside flat admission and
; must retain the ordinary matcher as the authority.
!(assertEqualToResult
  (size
    (collapse
      (match &native-dead-binding-count (pair (wrap fixed) tail) hit)))
  (2))
; Repeated open query variables are observable equality constraints, so the
; distinct-wildcard projection declines and ordinary matching remains exact.
!(assertEqualToResult
  (size (collapse (match &native-dead-binding-count (pair $q $q) hit)))
  (2))

; A pure conjunction observed only by strict size is a streaming count fold.
; Cross-leg bindings remain live along one path, rigid later legs contribute
; occurrence factors, and the completed BindingSet is never required.
!(bind! &native-conjunction-count (new-space native))
!(add-atom &native-conjunction-count (edge a b))
!(add-atom &native-conjunction-count (edge a b))
!(add-atom &native-conjunction-count (edge a c))
!(add-atom &native-conjunction-count (edge b d))
!(add-atom &native-conjunction-count (edge c d))
!(assertEqualToResult
  (size
    (collapse
      (match &native-conjunction-count
             (, (edge $x $y) (edge $y $z))
             (path $x $z))))
  (3))
; The same fold passes through a private let-bound tuple presentation.  This
; is the form emitted by the PeTTa-to-HE translator for length-after-collapse.
!(assertEqualToResult
  (let $paths
    (collapse
      (match &native-conjunction-count
             (, (edge $x $y) (edge $y $z))
             (path $x $z)))
    (size-atom $paths))
  (3))
!(assertEqualToResult
  (size
    (collapse
      (match &native-conjunction-count
             (, (edge $x $x) (edge $x d))
             hit)))
  (0))
!(add-atom &native-conjunction-count (same-edge $stored $stored))
!(add-atom &native-conjunction-count (same-edge $stored $stored))
!(assertEqualToResult
  (size
    (collapse
      (match &native-conjunction-count
             (, (same-edge a $middle) (edge $middle b))
             hit)))
  (4))

; A nested match template is an active producer, not one inert row.  The
; private-let presentation must retain its inner multiplicity and therefore
; decline the direct outer-row count.
!(add-atom &native-conjunction-count (tag a red))
!(add-atom &native-conjunction-count (tag a red))
!(add-atom &native-conjunction-count (tag a red))
!(assertEqualToResult
  (let $nested
    (collapse
      (match &native-conjunction-count (edge $x b)
        (match &native-conjunction-count (tag $x red)
          (joined $x))))
    (size-atom $nested))
  (6))
