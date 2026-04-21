; Regression test: equation variable hygiene (same-spelling capture)
;
; Bug: When an outer equation uses $a1 and an inner equation also uses $a1,
; the inner $a1 should be independent. A same-spelling fallback in
; bindings_resolve_query_visible_var incorrectly captured outer bindings.
;
; This test isolates the bug with minimal setup:
; 1. Outer match binds $a1 to a type containing $p#N
; 2. Inner equation's match should create fresh $a1 with $q#M (different epoch)
; 3. Bug: $q#M was incorrectly rewritten to $p#N due to same-spelling capture

; Setup: type repository
!(bind! &types (new-space))
!(add-atom &types (: foo (-> (T $x) (T $x))))
!(add-atom &types (: bar (-> (U $y) (U $y))))

; Inner equation: looks up bar's type, returns first arg type
(= (inner-lookup $space)
   (match $space (: bar (-> $a1 $ret))
     $a1))

; Outer equation: looks up foo's type, then calls inner-lookup
; The $a1 here should NOT interfere with inner-lookup's $a1
(= (outer-with-same-varname $space)
   (match $space (: foo (-> $a1 $ret))
     (inner-lookup $space)))

; Control: same logic but inner uses different variable name
(= (inner-lookup-renamed $space)
   (match $space (: bar (-> $arg1 $ret))
     $arg1))

(= (outer-with-different-varname $space)
   (match $space (: foo (-> $a1 $ret))
     (inner-lookup-renamed $space)))

; Test 1: Direct inner-lookup (no nesting) - baseline
; Should return (U $y#N) for some epoch N
!(assertAlphaEqualToResult
  (inner-lookup &types)
  ((U $y)))

; Test 2: Outer with DIFFERENT variable names - should work
; Inner's $arg1 = (U $y#M), outer's $a1 = (T $x#N) - independent
!(assertAlphaEqualToResult
  (outer-with-different-varname &types)
  ((U $y)))

; Test 3: Outer with SAME variable names - the bug case
; Inner's $a1 should still be (U $y#M), not captured by outer's $a1 = (T $x#N)
; BUG: Without fix, this returned (T $x#N) or (U $x#N) due to capture
!(assertAlphaEqualToResult
  (outer-with-same-varname &types)
  ((U $y)))

; Test 4: Deeper nesting - triple same-name
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

; Test 5: Verify variables are actually fresh (not accidentally equal)
; Add second type with same structure but different constructor
!(add-atom &types (: baz (-> (V $z) (V $z))))

(= (get-bar-arg $space) (match $space (: bar (-> $a1 $r)) $a1))
(= (get-baz-arg $space) (match $space (: baz (-> $a1 $r)) $a1))

; Nested: outer matches bar, inner matches baz - both use $a1
(= (bar-then-baz $space)
   (match $space (: bar (-> $a1 $r1))
     (get-baz-arg $space)))

!(assertAlphaEqualToResult
  (bar-then-baz &types)
  ((V $z)))  ; Should be baz's type, not bar's

; Test 6: The actual bug - equation call with same-named pattern vars
; Outer matches foo, binds $a1. Then CALLS an equation whose internal
; pattern also uses $a1. The equation's $a1 should be independent.
(= (lookup-bar-arg $space)
   (match $space (: bar (-> $a1 $ret)) $a1))

(= (outer-calls-equation $space)
   (match $space (: foo (-> $a1 $ret))
     (lookup-bar-arg $space)))  ; equation's $a1 is DIFFERENT scope

!(assertAlphaEqualToResult
  (outer-calls-equation &types)
  ((U $y)))  ; bar's arg, not foo's - equation has its own scope
