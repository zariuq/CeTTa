!(import! &self pf)

; The kernel's universes are the same under every profile: each is a type
; of the next, and none is a type of itself.  Whether a universe is also a
; set is a question for the set face.  The HOTG profile has a universe around
; every set; the common profile has no universe operator, and leaves the
; question unevaluated.  In the mathematical model the universes of a level
; are exactly the von Neumann levels at inaccessible cardinals, and the
; kernel's tower is interpreted under the named hypothesis that such levels
; are cofinal; the kernel itself assumes neither.

!(bind! &common (new-space))
!(bind! &hotg (let $s (new-space) (let $_ (add-atom $s (hol:profile megalodon-hotg)) $s)))
!(bind! &hol (let $s (new-space) (let $_ (add-atom $s (hol:profile core)) $s)))

; 1. The kernel's universes, under three profiles.
!(type:check &common (u 0) (u 1))
!(type:check &hotg (u 0) (u 1))
!(type:check &hol (u 0) (u 1))
!(type:check &common (u 1) set)
!(type:check &hotg (u 1) set)
!(type:check &common (u 0) (u 0))

; 2. The set face: a universe around a set.
!(hol:of &hotg (UnivOf Empty))
!(hol:proves &hotg (pf:by universeIn) (In Empty (UnivOf Empty)))
!(set:of &common (UnivOf Empty))
