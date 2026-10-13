(hol:profile base)

; Where a native route answers `type:of`, `get-type` answers the HE reading of
; the same declarations, never a contradiction of the native answer.  Each
; line asks both, `get-type` first:
;
;   (Law case (get-type answers) (type:of answers))

(: a (-> u0 u0))
(: ab (-> (-> u0 u0) (-> u0 (-> u0 u0))))
(: p u0)
(: h (id u0 p p))

!(Law arrow (collapse (get-type a)) (collapse (type:of &self a)))
!(Law curried (collapse (get-type ab)) (collapse (type:of &self ab)))
!(Law dependent (collapse (get-type h)) (collapse (type:of &self h)))
!(Law application (collapse (get-type (ab a))) (collapse (type:of &self (ab a))))

; Conflicting declarations mint no native evidence; both judgments keep the
; HE reading, in which each declaration is its own constant.
(: c u0)
(: c (-> u0 u0))
!(Law conflict (collapse (get-type c)) (collapse (type:of &self c)))
!(hol:of &self c)

; Over another space, `get-type-space` answers as `get-type` does over its
; own.
!(bind! &s (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile base)) $profile-space)))
!(add-atom &s (: k (-> u0 u0)))
!(Law named-space (collapse (get-type-space &s k)) (collapse (type:of &s k)))

; Prime's own `pair`, a datatype's constructor, keeps its native typing, and
; `get-type` reads it back in HE's flat form.
!(hol:inductive &self num (u 0) (: zero num) (: suc (-> num num)))
!(hol:inductive &self (Pair (: A set) (: B set)) set (: pair (-> A B (Pair A B))))
!(Law native-former (collapse (get-type (pair num num zero zero))) (collapse (type:of &self (pair num num zero zero))))
