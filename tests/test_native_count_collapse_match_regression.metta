; Regression: counting a collapsed native-space match should count bindings
; directly instead of materializing and re-evaluating a large result tuple.
;
; `size` is STRICT: it evaluates (collapse (match ...)) and counts the tuple.
; The runtime streams the match rows and counts them in O(1) memory rather than
; materializing the O(N) tuple, so `size` reports the row count (512).
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

!(reset-runtime-stats!)
; Strict `size` over the collapsed match: streamed count = 512.
!(assertEqualToResult
  (size (collapse (match &native-count (friend $y $x) hit)))
  (512))
; Non-strict `size-atom` over the unevaluated (collapse ...) = its arity = 2.
!(assertEqualToResult
  (size-atom (collapse (match &native-count (friend $y $x) hit)))
  (2))
; Evaluate the collapse first (let), then `size-atom` counts the tuple = 512.
!(assertEqualToResult
  (let $c (collapse (match &native-count (friend $y $x) hit)) (size-atom $c))
  (512))
!(bind! &stats (runtime-stats!))
!(assertEqualToResult
  (match &stats
         (runtime-counter eval-c-stack-guard-depth-peak $n)
         (< $n 64))
  (True))
