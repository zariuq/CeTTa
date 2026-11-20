; Stored rows: an ordinary row is a schema whose variables are private to each
; read; (capture (V ...) ROW) keeps the identities of V ... in the stored row.
!(bind! &s (new-space))
; T1: the row's variables are not the writer's.  One answer, m.
!(let $_ (add-atom &s (q $v $w $w m)) (count (reify (match &s (q kc (fz $v) (fz kb) $m) $m))))
!(let $_ (add-atom &s (q2 $v $w $w m)) (match &s (q2 kc (fz $v) (fz kb) $m) $m))
; Two reads in one branch open two copies.
!(let $_ (add-atom &s (row $x)) (let $p (match &s (row a) a) (let $q (match &s (row b) b) (Pair $p $q))))
!(match &s (row b) later)
; One read keeps the aliases of one copy.
!(add-atom &s (pair $x $x))
!(match &s (pair a $y) $y)
!(count (reify (match &s (pair a b) mismatch)))
; A later binding of the writer's variable does not reach the stored row.
!(let $_ (add-atom &s (later $v)) (unify $v 1))
!(count (reify (match &s (later 2) yes)))
; A binding made before the write is part of the row.
!(chain (unify $v 5) $_ (add-atom &s (bound $v)))
!(count (reify (match &s (bound 6) yes)))
!(count (reify (match &s (bound 5) yes)))
; A removal selects the row its text would have stored: variables up to renaming.
!(add-atom &s (twin $a $a))
!(remove-atom &s (twin $b $c))
!(count (reify (match &s (twin $p $q) present)))
!(remove-atom &s (twin $b $b))
!(count (reify (match &s (twin $p $q) present)))
; An explicit capture: reads in one branch share it, separate requests do not
; commit what a read binds.
!(bind! &k (new-space))
!(add-atom &k (capture ($x) (cap $x)))
!(count (reify (chain (match &k (cap red) one) $_ (match &k (cap blue) two))))
!(count (reify (chain (match &k (cap red) one) $_ (match &k (cap red) two))))
!(match &k (cap red) seen)
!(match &k (cap blue) seen)
; One capture kept by two rows is one identity.
!(let $_ (add-atom &k (capture ($y) (left $y))) (add-atom &k (capture ($y) (right $y))))
!(chain (match &k (left green) l) $_ (match &k (right $c) $c))
; A mixed row: the capture is shared, the private variable is fresh per read.
!(add-atom &k (capture ($x) (mix $x $z)))
!(count (reify (chain (match &k (mix n b) one) $_ (match &k (mix n c) two))))
!(count (reify (chain (match &k (mix n b) one) $_ (match &k (mix o c) two))))
; Capturing a bound name keeps nothing: its value is stored.
!(let $x 5 (add-atom &k (capture ($x) (fixed $x))))
!(count (reify (match &k (fixed 5) yes)))
; A firing commits what it learns about a capture.
!(bind! &d (new-space))
!(add-atom &d (capture ($c) (color $c)))
!(count (transform &d (, (read (color $c)) (let True (unify $c red))) noted))
!(count (reify (match &d (color blue) blue)))
!(match &d (color $value) $value)
; A firing cannot give a committed capture another value.
!(count (transform &d (, (read (color $c)) (let True (unify $c blue))) refused))
; A firing's read alone binds a capture, and commits it.
!(add-atom &d (capture ($c) (shade $c)))
!(count (transform &d (read (shade dark)) chosen))
!(count (reify (match &d (shade light) other)))
; A firing that makes a capture: the new row keeps the firing's unknown.
!(add-atom &d (task one))
!(count (transform &d (take (task $t)) (capture ($w) (claim $t $w))))
!(count (reify (chain (match &d (claim one alice) a) $_ (match &d (claim one bob) b))))

; Per-row capture roles, stable occurrence order, transitive tells and selected footprints.
!(bind! &roles (new-space))
!(let $_ (add-atom &roles (capture ($x) (shared-row $x)))
   (add-atom &roles (schema-row $x)))
!(count (reify (chain (match &roles (shared-row red) seen) $_
   (match &roles (schema-row blue) independent))))

!(bind! &ordered (new-space))
!(add-atom &ordered (capture ($x) (event first $x)))
!(add-atom &ordered (event second fixed))
!(match &ordered (event $tag $value) $tag)
!(count (transform &ordered (read (event first bound)) recorded))
!(match &ordered (event $tag $value) $tag)

!(bind! &aliases (new-space))
!(let $_ (add-atom &aliases (capture ($a $b) (owner $a $b)))
   (add-atom &aliases (capture ($b) (held $b))))
!(count (transform &aliases (read (owner (box $v) $v)) linked))
!(count (transform &aliases (read (held red)) bound))
!(count (reify (match &aliases (owner (box blue) red) impossible)))
!(match &aliases (owner $p $q) (Pair $p $q))

!(bind! &footprint (new-space))
!(add-atom &footprint (capture ($x) (state ignored $x)))
!(add-atom &footprint (state used fixed))
!(chain (match &footprint (state ignored trial) seen) $_
   (count (transform &footprint (read (state used $u)) selected)))
!(count (reify (match &footprint (state ignored alternative) still-open)))
