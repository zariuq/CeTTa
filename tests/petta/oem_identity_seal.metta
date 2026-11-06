; A capsule's payload reached through the compiled tier keeps its variable:
; two openings share it, and it is the stored variable, as without the tier.
!(import! &self seal)
!(let $f (seal:new demo) (bind! &f (new-state $f)))
(= (S) (let (seal:facets $s $u) (get-state &f) $s))
(= (U) (let (seal:facets $s $u) (get-state &f) $u))
!(let $c (seal:seal (S) (foo $x bar)) (bind! &cap (new-state $c)))
(= (open $k) (seal:unseal (U) (get-state &cap)))
; uncached reference: two openings share the payload variable
!(let $p (open 1) (let $q (open 2) (== $p $q)))
!(let $p (open 1) (== $p (seal:unseal (U) (get-state &cap))))
(= (openTwice $k) (let $p (open $k) (let $q (open $k) ($p $q))))
!(let ($p $q) (openTwice 1) ((== $p $q) (== $p (seal:unseal (U) (get-state &cap)))))
