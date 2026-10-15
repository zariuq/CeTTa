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

; An explicitly captured value in a later pattern is read in its retained
; world. Uncaptured pattern names still introduce fresh lexical binders.
!(let $part (cons-atom Part (A B))
   (reify (let (Box $part) (Box (Part A B)) Matched){$part}))
!(let $part (cons-atom Part (A B))
   (reify (let (Box $part) (Box (Part A C)) MustNotMatch){$part}))
(= (prime-need:match-earned $part)
   (let (Box $part) (Box (Part A B)) Matched){$part})
!(let $part (cons-atom Part (A B)) (prime-need:match-earned $part))

; Reading a computed pattern exposes data without executing its written code.
!(let $code (cons-atom + (1 2))
   (let $box (cons-atom Box ($code))
     (reify (let (Box $code) $box MatchedCode){$code})))
!(let $code (cons-atom + (1 2))
   (reify (let (Box $code) (Box 3) MustNotMatch){$code}))
!(let $code (cons-atom println! (PatternMustRemainData))
   (let $box (cons-atom Box ($code))
     (reify (let (Box $code) $box MatchedEffectData){$code})))
!(let $part (cons-atom Part (A C)) (reify (prime-need:match-earned $part)))
