; lib_seal against docs/seal.md: facets and
; capsules are opaque values with one identity relation; misses are no
; answer and faults are errors (here caught by the try- forms); printed
; text, numbers and serialization never recreate authority; payloads are
; retained descriptors, and a sealed space is a live reference; key-locked
; services check the current policy on every opening.  Runs in HE and PeTTa.
!(import! &self seal)
!(let $f (seal:new demo) (bind! &f (new-state $f)))
!(let $f (seal:new other) (bind! &g (new-state $f)))
(= (S) (let (seal:facets $s $u) (get-state &f) $s))
(= (U) (let (seal:facets $s $u) (get-state &f) $u))
(= (S2) (let (seal:facets $s $u) (get-state &g) $s))
(= (U2) (let (seal:facets $s $u) (get-state &g) $u))

; Facets print their nickname, which is display text only.
!(S)
!(U)
!((seal:kind (S)) (seal:kind (U)) (seal:same-brand (S) (U)) (seal:same-brand (S) (S2)))

; Unsealing returns the payload as written; variables keep their identity.
!(let $c (seal:seal (S) (foo $x bar)) ($c (seal:unseal (U) $c)))
!(let $c (seal:seal (S) (foo $x bar)) (let $p (seal:unseal (U) $c) (let $q (seal:unseal (U) $c) (== $p $q))))
; Fresh seals differ; a copy is the same capsule.
!(let $c1 (seal:seal (S) data) (let $c2 (seal:seal (S) data) (== $c1 $c2)))
!(let $c1 (seal:seal (S) data) (let $c2 $c1 (== $c1 $c2)))

; Recognition misses are no answer: printed text, a number, another brand.
!(collapse (seal:unseal (U) (Sealed demo)))
!(collapse (seal:unseal (U) 42))
!(let $c (seal:seal (S2) secret) (collapse (seal:unseal (U) $c)))

; Faults: a missing right or a wrong facet.
!(let $c (seal:seal (S) x) (case (seal:try-unseal (S) $c) (((Error $call $why) (fault $why)) ($r (opened $r)))))
!(case (seal:try-seal (U) x) (((Error $call $why) (fault $why)) ($r (sealed $r))))
!(let $none (seal:attenuate (U) none) (let $c (seal:seal (S) x)
   (case (seal:try-unseal $none $c) (((Error $call $why) (fault $why)) ($r (opened $r))))))
!(let $c (seal:seal (S) x) (case (seal:try-seal $c y) (((Error $call $why) (fault $why)) ($r (sealed $r)))))
; Attenuation never adds a right: the unsealer asked for the seal right has
; none.
!(let $u (seal:attenuate (U) seal)
   ($u (case (seal:try-seal $u y) (((Error $call $why) (fault $why)) ($r (sealed $r))))))

; Matching sees a capsule as an opaque leaf: no pattern opens it.
!(let $c (seal:seal (S) x) (case $c (((Sealed $n) (opened $n)) ($other closed))))
; Printing gives display text; reading it back gives data, not a capsule.
!(let $c (seal:seal (S) x) (repr $c))
!(let $c (seal:seal (S) x) (let $text (repr $c) (seal:kind (sread $text))))

; One identity relation in a space: two stored occurrences stay two answers,
; a repeated variable matches the same capsule only, and removal behaves for
; capsules exactly as for ordinary atoms of the dialect.
!(let $c (seal:seal (S) one) (bind! &c1 (new-state $c)))
!(let $c (seal:seal (S) one) (bind! &c2 (new-state $c)))
!(bind! &store (new-space))
!(let $x (get-state &c1) (add-atom &store (holds $x)))
!(let $x (get-state &c1) (add-atom &store (holds $x)))
!(let $x (get-state &c2) (add-atom &store (holds $x)))
!(add-atom &store (holds a))
!(add-atom &store (holds a))
!(let $x (get-state &c1) (add-atom &store (pair $x $x)))
!(let $x (get-state &c1) (let $y (get-state &c2) (add-atom &store (pair $x $y))))
!(let $x (get-state &c1) (let $found (collapse (match &store (holds $x) found)) (size-atom $found)))
!(let $found (collapse (match &store (pair $x $x) same)) $found)
!(let $x (get-state &c1) (remove-atom &store (holds $x)))
!(remove-atom &store (holds a))
!(let $x (get-state &c1) (let $capsules (collapse (match &store (holds $x) found))
   (let $plain (collapse (match &store (holds a) found)) ((size-atom $capsules) (size-atom $plain)))))
!(let $x (get-state &c2) (let $found (collapse (match &store (holds $x) found)) (size-atom $found)))

; Serialization refuses authority, however deeply nested; text is data.
!(case (seal:try-portable (data (nested (Sealed demo)))) (((Error $call $why) (fault $why)) ($r (portable $r))))
!(let $c (seal:seal (S) x) (case (seal:try-portable (data (nested $c))) (((Error $call $why) (fault $why)) ($r (portable $r)))))
!(case (seal:try-portable (keyed (U))) (((Error $call $why) (fault $why)) ($r (portable $r))))
!(let $k (seal:new-key) (case (seal:try-portable (a key $k)) (((Error $call $why) (fault $why)) ($r (portable $r)))))

; A sealed space is the live space: not a snapshot, not frozen.
!(bind! &sp (new-space))
!(add-atom &sp (fact 1))
!(let $c (seal:seal (S) &sp) (let $back (seal:unseal (U) $c) (let $_ (add-atom $back (fact 2))
   (let $facts (collapse (match &sp (fact $n) $n)) $facts))))

; A key-locked service checks the current policy at every opening.
!(let $v (seal:new-service vault) (bind! &svc (new-state $v)))
(= (USER) (let (seal:service $user $admin $sealer) (get-state &svc) $user))
(= (ADMIN) (let (seal:service $user $admin $sealer) (get-state &svc) $admin))
(= (LOCKER) (let (seal:service $user $admin $sealer) (get-state &svc) $sealer))
!(let $k (seal:new-key) (bind! &key (new-state $k)))
!(let $k (seal:new-key) (bind! &other-key (new-state $k)))
!(let $b (seal:lock (LOCKER) (get-state &key) (secret 42)) (bind! &box (new-state $b)))
(= (try-open $key) (case (seal:try-open (USER) (get-state &box) $key) (((Error $call $why) (fault $why)) ($r (opened $r)))))
!(try-open (get-state &key))
!(seal:grant (ADMIN) (get-state &key) once)
!(try-open (get-state &other-key))
!(try-open (Key))
!(try-open (get-state &key))
!(try-open (get-state &key))
!(seal:grant (ADMIN) (get-state &key) always)
!(try-open (get-state &key))
!(seal:revoke (ADMIN) (get-state &key))
!(try-open (get-state &key))
; The service's own unsealer is private: the user capability is not a facet
; and opens nothing directly.
!(case (seal:try-unseal (USER) (get-state &box)) (((Error $call $why) (fault $why)) ($r (opened $r))))

; An equation added for a library name adds answers, not authority.
(= (seal:unseal $unsealer $capsule) (forged))
!(let $c (seal:seal (S2) secret) (collapse (seal:unseal (U) $c)))
!(seal:kind (forged))
