; A declaration whose subject holds variables types every atom it unifies
; with, as the query (: atom $t) reads declarations: a patterned subject's
; type is instantiated by the meeting, and a subject the atom does not
; unify with says nothing of it.
(: ($h z) Zed)
(: (List $t) (Type $t))
(: (f a) T)
!(get-type (s z))
!(get-type (List Number))
!(get-type (s y))
!(get-type z)
!(get-type (f b))
!(get-type (f a))
