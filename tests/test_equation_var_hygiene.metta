; Regression test: equation variable hygiene (same-spelling capture)
;
; Bug: When an outer equation uses $a1 and an inner equation also uses $a1,
; the inner $a1 should be independent. A same-spelling fallback in
; bindings_resolve_query_visible_var incorrectly captured outer bindings.

!(bind! &types (new-space))
!(add-atom &types (: foo (-> (T $x) (T $x))))
!(add-atom &types (: bar (-> (U $y) (U $y))))

(= (inner-lookup $space)
   (match $space (: bar (-> $a1 $ret))
     $a1))

(= (outer-with-same-varname $space)
   (match $space (: foo (-> $a1 $ret))
     (inner-lookup $space)))

(= (inner-lookup-renamed $space)
   (match $space (: bar (-> $arg1 $ret))
     $arg1))

(= (outer-with-different-varname $space)
   (match $space (: foo (-> $a1 $ret))
     (inner-lookup-renamed $space)))

!(assertAlphaEqualToResult
  (inner-lookup &types)
  ((U $y)))

!(assertAlphaEqualToResult
  (outer-with-different-varname &types)
  ((U $y)))

!(assertAlphaEqualToResult
  (outer-with-same-varname &types)
  ((U $y)))

(= (deep-inner $space)
   (match $space (: bar (-> $a1 $ret)) $a1))

(= (deep-middle $space)
   (match $space (: foo (-> $a1 $ret))
     (deep-inner $space)))

(= (deep-outer $space)
   (match $space (: foo (-> $a1 $ret))
     (deep-middle $space)))

!(assertAlphaEqualToResult
  (deep-outer &types)
  ((U $y)))

!(add-atom &types (: baz (-> (V $z) (V $z))))

(= (get-baz-arg $space)
   (match $space (: baz (-> $a1 $r)) $a1))

(= (bar-then-baz $space)
   (match $space (: bar (-> $a1 $r1))
     (get-baz-arg $space)))

!(assertAlphaEqualToResult
  (bar-then-baz &types)
  ((V $z)))

(= (lookup-bar-arg $space)
   (match $space (: bar (-> $a1 $ret)) $a1))

(= (outer-calls-equation $space)
   (match $space (: foo (-> $a1 $ret))
     (lookup-bar-arg $space)))

!(assertAlphaEqualToResult
  (outer-calls-equation &types)
  ((U $y)))
