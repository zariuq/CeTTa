; Which relations are tabled or memoized is part of the program: a change
; to it, or to the equations, starts a new revision.  Tables answered before
; it are dropped, never replayed, and a call reads the current program, also
; within the directive that made the change.
!(import! &self (library lib_tabling))
!(import! &self (library lib_memo))
!(memoize h)
(= (h $x) $x)
!(collapse (h 3))
; A memoized call made again after an equation is added is answered afresh.
(= (h $x) (s $x))
!(sort-atom (collapse (h 3)))
; A relation registered as tabled and called in the same directive answers.
(= (g $x) $x)
(= (g $x) (s $x))
!(let $_ (tabled (g $x)) (sort-atom (collapse (g 1))))
; The memoized call after that registration still reads the current program.
!(sort-atom (collapse (h 3)))
