; Test that stdlib equations are introspectable via match
; This proves the precompiled stdlib is loaded into the space as equations.

; Query: how many if-equations are in the space?
!(assertEqual
  (size-atom (collapse (match &self (= (if $c $a $b) $r) $c)))
  2)

; Query: what conditions does if handle?
!(assertEqualToResult
  (match &self (= (if $c $a $b) $r) $c)
  (True False))

; Query: `if` has a type declaration in the space (at least 1)
!(assertEqual
  (> (size-atom (collapse (match &self (: if $type) $type))) 0)
  True)

; Verify stdlib `if` works normally
!(assertEqual (if True yes no) yes)
!(assertEqual (if False yes no) no)

; Verify car-atom / cdr-atom (still hardcoded but with stdlib type decls)
!(assertEqual (car-atom (a b c)) a)
!(assertEqual (cdr-atom (a b c)) (b c))
