; Cross-engine HE-parity golden for `(size-atom (collapse (match ...)))`.
;
; `size-atom` is non-strict (argument taken UNEVALUATED), so the DIRECT form is
; the structural size of the literal 2-element expression `(collapse (match ...))`
; -> 2 on every engine, exactly as stock HE.  The LET form binds the evaluated
; collapse and applies `size-atom` to the materialized tuple, so it is the
; evaluated count -> 3 on every engine.  Both hold for a variable template
; `$x` and a ground template `hit`.
;
; A former "count-fusion" fast path rewrote the direct form into the match
; count, diverging from HE and disagreeing across engines; this golden locks the
; native and pathmap engines to the HE-parity answers.
;
; The mork engine is not selectable through `(new-space mork)` -- the mork
; syntax is the distinct `(mork:new-space)`/`(mork:*)` family -- so it is out of
; this selector's scope.

; ---- native engine ----
!(bind! &xn (new-space native))
!(add-atom &xn (bench-seen a))
!(add-atom &xn (bench-seen b))
!(add-atom &xn (bench-seen c))
!(assertEqualToResult
  (size-atom (collapse (match &xn (bench-seen $x) $x)))
  (2))
!(assertEqualToResult
  (let $c (collapse (match &xn (bench-seen $x) $x)) (size-atom $c))
  (3))
!(assertEqualToResult
  (size-atom (collapse (match &xn (bench-seen $x) hit)))
  (2))
!(assertEqualToResult
  (let $c (collapse (match &xn (bench-seen $x) hit)) (size-atom $c))
  (3))

; ---- pathmap engine ----
!(bind! &xp (new-space pathmap))
!(add-atom &xp (bench-seen a))
!(add-atom &xp (bench-seen b))
!(add-atom &xp (bench-seen c))
!(assertEqualToResult
  (size-atom (collapse (match &xp (bench-seen $x) $x)))
  (2))
!(assertEqualToResult
  (let $c (collapse (match &xp (bench-seen $x) $x)) (size-atom $c))
  (3))
!(assertEqualToResult
  (size-atom (collapse (match &xp (bench-seen $x) hit)))
  (2))
!(assertEqualToResult
  (let $c (collapse (match &xp (bench-seen $x) hit)) (size-atom $c))
  (3))
