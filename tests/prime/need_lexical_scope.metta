; Refinement of the exact captured identity must reach the delayed body.
!(let $promise (delay (noreduce-eq $x A))
   (unify $x A (force $promise) Empty))

; The equation creates a fresh private identity. The caller's same-spelled
; $x is distinct and must not capture it when the promise is demanded.
(= (prime-need:private-x) (delay (noreduce-eq $x A)))
!(let $promise (prime-need:private-x)
   (unify $x A (force $promise) Empty))

; Renaming only that unrelated caller variable must preserve the answer.
!(let $promise (prime-need:private-x)
   (unify $y A (force $promise) Empty))
