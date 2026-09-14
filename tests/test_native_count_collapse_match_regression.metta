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
