; A stored variable keeps one identity in the compiled tier whichever route
; reaches it first: a host answer or a state read.
!(import! &self seal)
!(let $f (seal:new demo) (bind! &f (new-state $f)))
(= (S) (let (seal:facets $s $u) (get-state &f) $s))
(= (U) (let (seal:facets $s $u) (get-state &f) $u))
!(bind! &raw (new-state (foo $y)))
!(let $v (get-state &raw) (let $c (seal:seal (S) $v) (bind! &cap (new-state $c))))
; reference: the capsule payload is the stored value, one variable
!(== (seal:unseal (U) (get-state &cap)) (get-state &raw))
(= (open $k) (seal:unseal (U) (get-state &cap)))
(= (hostThenState $k) (let $p (open $k) (let $r (get-state &raw) ($p $r))))
(= (stateThenHost $k) (let $r (get-state &raw) (let $p (open $k) ($p $r))))
!(let ($p $r) (hostThenState 1) ((== $p $r) (== $r (get-state &raw)) (== $p (get-state &raw))))
!(let ($p $r) (stateThenHost 1) ((== $p $r) (== $r (get-state &raw)) (== $p (get-state &raw))))
(= (stateOnly $k) (get-state &raw))
!(let $r (stateOnly 1) (== $r (get-state &raw)))
(= (hostOnly $k) (open $k))
!(let $p (hostOnly 1) (== $p (get-state &raw)))
