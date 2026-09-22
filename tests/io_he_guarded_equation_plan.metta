(= (ctor $__fresh) (unify $__fresh (A $x) (ctor-ok $x) Empty))
(= (shared $x $__fresh) (unify $__fresh (A $x) shared-ok Empty))
(= (two $__left $__right)
   (unify $__left (L $x)
     (unify $__right (R $x) (two-ok $x) Empty)
     Empty))

; This intermediate escapes into the result, so the sequential guard remains
; authoritative.
(= (retained $__fresh)
   (unify $__fresh (A $x) (retained-ok $__fresh $x) Empty))

!(ctor (A B))
!(ctor (C B))
!(ctor $query)
!(shared B (A B))
!(shared B (A C))
!(two (L X) (R X))
!(two (L X) (R Y))
!(retained (A B))
