; An explicit unsupported selection has no set interpretation. Even malformed
; submissions remain syntax until an interpretation exists.
(set:profile none)
!(set:signature)
!(set:signature-digest)
!(set:of Empty)
!(set:check Empty prop)
!(set:proves (pf:all-elim (pf:known universeIn) Empty) (In Empty (UnivOf Empty)))
!(set:known-proposition setInduction)
!(set:proves)
!(type:of Empty)
!(type:formed (u 0))

; An unsupported profile is a declaration, not a fallback to another theory.
!(bind! &future (new-space))
!(add-atom &future (set:profile future-theory))
!(let $r (set:of &future Empty) (car-atom $r))
!(let $r (try (set:check &future Empty prop)) (car-atom $r))

; Ordinary authored operations with a set: prefix still compute.
(= (set:echo $x) $x)
!(set:echo 17)
