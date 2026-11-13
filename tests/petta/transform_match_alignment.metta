!(import! &self (library lib_transform))
(= (show $s) (sort-atom (collapse (match $s $a $a))))

; Reads alone: a transform answers as match does, duplicates included.
!(bind! &p (new-space))
!(add-atom &p (p A))
!(add-atom &p (p B))
!(add-atom &p (p A))
!(add-atom &p (q B))
!(reads-transform (collapse (transform &p (p $x) (tag $x))))
!(reads-match (collapse (match &p (p $x) (tag $x))))
!(join-transform (collapse (transform &p (, (p $x) (q $x)) (both $x))))
!(join-match (collapse (match &p (, (p $x) (q $x)) (both $x))))
!(absent-transform (collapse (transform &p (, (p $x) (absent (q $x))) (alone $x))))
!(unique-transform (collapse (transform &p (unique (q $x)) (only $x))))
!(no-match (collapse (transform &p (r $x) (never $x))))

; Takes: one branch per match, each taking its own occurrence.
!(bind! &a (new-space))
!(add-atom &a (coin 1))
!(add-atom &a (coin 2))
!(add-atom &a (coin 5))
!(spend-each (sort-atom (collapse (transform &a (take (coin $v)) (got $v)))))
!(after-each (show &a))

; Two copies of one atom are two matches, and each takes one copy.
!(bind! &d (new-space))
!(add-atom &d (coin 1))
!(add-atom &d (coin 1))
!(add-atom &d (coin 2))
!(spend-copies (collapse (transform &d (take (coin 1)) spent)))
!(after-copies (show &d))

; once fires one match that holds: one coin is taken, and it and the coin left
; are the two stored.  Which match is tried first is the engine's.
!(bind! &o (new-space))
!(add-atom &o (coin 1))
!(add-atom &o (coin 2))
!(spend-once (let $got (collapse (once (transform &o (take (coin $v)) $v)))
   (spent (size-atom $got) (sort-atom (cons-atom (car-atom $got) (collapse (match &o (coin $w) $w)))))))

; Two matches want the one token: the branch that runs first takes it, and the
; other finds its match no longer holds and fires nothing.
!(bind! &t (new-space))
!(add-atom &t (tok a))
!(add-atom &t (k 1))
!(add-atom &t (k 2))
!(contend (let $won (collapse (transform &t (, (take (tok $x)) (k $y)) (won $x $y)))
   (contended (size-atom $won) (collapse (let (won $x $y) (superpose $won) $x)))))
!(after-contend (show &t))

; all is computed when a branch fires: the branch that runs first takes every q,
; and the other fires with none left, so every move is one branch's.
!(bind! &e (new-space))
!(add-atom &e (r 1))
!(add-atom &e (r 2))
!(add-atom &e (q a))
!(add-atom &e (q b))
!(all-per-branch (let $m (collapse (transform &e (, (r $x) (all (take (q $y)))) (moved $x $y)))
   (moved (size-atom (unique-atom (collapse (let (moved $x $y) (superpose $m) $x))))
          (sort-atom (collapse (let (moved $x $y) (superpose $m) $y))))))
!(after-all (show &e))

; Into a destination space: each branch answers how many it routed.
!(bind! &src (new-space))
!(bind! &dst (new-space))
!(add-atom &src (job a))
!(add-atom &src (job b))
!(routed (collapse (transform &src (take (job $j)) (done $j) &dst)))
!(after-src (show &src))
!(after-dst (show &dst))

; once fires one match that holds, as a source too: alone it takes one coin;
; after a read, each branch takes a q still there.
!(bind! &w (new-space))
!(add-atom &w (coin 1))
!(add-atom &w (coin 2))
!(add-atom &w (coin 5))
!(once-marker (let $got (collapse (transform &w (once (take (coin $v))) $v))
   (spent (size-atom $got) (sort-atom (cons-atom (car-atom $got) (collapse (match &w (coin $c) $c)))))))
!(bind! &f (new-space))
!(add-atom &f (r 1))
!(add-atom &f (r 2))
!(add-atom &f (q a))
!(add-atom &f (q b))
!(add-atom &f (q c))
!(once-per-branch (let $m (collapse (transform &f (, (r $x) (once (take (q $y)))) (moved $x $y)))
   (moved (sort-atom (collapse (let (moved $x $y) (superpose $m) $x)))
          (size-atom (unique-atom (collapse (let (moved $x $y) (superpose $m) $y))))
          (size-atom (collapse (match &f (q $z) $z))))))
!(after-once-per-branch (sort-atom (collapse (match &f (r $x) $x))))

; A branch is the match it stands for: its other conditions are evaluated when it
; fires, each with its own variables free.  A collection whose variable a later
; conjunct binds still fires; a unique count sees what an earlier branch added.
!(bind! &g (new-space))
!(add-atom &g (q 1))
!(add-atom &g (q 2))
!(add-atom &g (r 1))
!(collect-bound-later (collapse (transform &g (, (collect (q $y) $C) (r $y)) (got $y (collected $C)))))
!(bind! &u (new-space))
!(add-atom &u (k 1))
!(add-atom &u (k 2))
!(add-atom &u (p 7))
!(unique-growth (collapse (transform &u (, (take (k $n)) (unique (p $x))) (p $n) &u)))
; one k is left, its number beside the p the branch that fired added and (p 7)
!(after-unique-growth (let $k (collapse (match &u (k $n) $n))
   (grown (size-atom $k) (size-atom (collapse (match &u (p $x) $x)))
          (sort-atom (cons-atom (car-atom $k) (collapse (match &u (p $x) $x)))))))

; The template's own syntax decides how many instances a row gives: a value bound to
; a comma expression is one instance.
!(bind! &cs (new-space))
!(bind! &cd (new-space))
!(add-atom &cs (pair (, a b)))
!(comma-value (collapse (transform &cs (pair $x) $x &cd)))
!(after-comma-value (show &cd))

; ',' is conjunction where a query is read and a constructor where data is.  A take
; of a conjunction takes its joint match and never makes an atom:
!(bind! &jt (new-space))
!(add-atom &jt a) !(add-atom &jt a) !(add-atom &jt b)
!(joint-take (collapse (transform &jt (take (, a b)) took)))
!(after-joint-take (show &jt))
; a stored atom headed by ',' is data, reached by a pattern that is not:
!(bind! &jc (new-space))
!(add-atom &jc (, a b))
!(stored-comma (collapse (transform &jc $x $x)))
!(stored-comma-match (collapse (match &jc $x $x)))
; reads of a conjunction may reuse an occurrence, as match's do:
!(bind! &jr (new-space))
!(add-atom &jr (p 1)) !(add-atom &jr (, (p 1) (p 1)))
!(read-pairs (size-atom (collapse (transform &jr (read (, $y $z)) ($y $z)))))
!(read-pairs-match (size-atom (collapse (match &jr (, $y $z) ($y $z)))))
; takes are distinct occurrences: (take (, a a)) needs two copies:
!(bind! &ja (new-space))
!(add-atom &ja a)
!(take-aa-one (collapse (transform &ja (take (, a a)) took)))
!(add-atom &ja a)
!(take-aa-two (collapse (transform &ja (take (, a a)) took)))
!(after-take-aa (show &ja))
