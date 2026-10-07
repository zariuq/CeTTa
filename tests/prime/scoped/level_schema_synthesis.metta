(set:profile hol)

; A declaration over universe levels is a schema the kernel reads.  When the
; kernel forms it, synthesis answers with its instance.  When it does not,
; the declared schema is not reported as the type: its level variables are
; unread, and a schema formed at no level types nothing.
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (set:profile hol)) $profile-space)))
!(set:inductive &s num (u 0) (: zero num) (: suc (-> num num)))
; Formed at every level: synthesis and checking succeed.
!(add-atom &s (: poly-id (-> (A : (u $l)) (-> A A))))
!(type:check &s poly-id (-> (A : (u 0)) (-> A A)))
!(type:check &s (poly-id num zero) num)
; Formed at level 0 only: the name does not resolve, and no type is claimed.
!(add-atom &s (: level-zero-only (-> (A : (u $l)) (-> (id (u 0) A A) A))))
!(let $r (try (type:of &s level-zero-only)) (car-atom $r))
; Formed at no level.
!(add-atom &s (: ill-formed (-> (A : (u $l)) (A A))))
!(let $r (try (type:of &s ill-formed)) (car-atom $r))
!(let $r (try (type:of &s (ill-formed num))) (car-atom $r))
