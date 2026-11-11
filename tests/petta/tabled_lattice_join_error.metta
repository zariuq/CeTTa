;; A lattice join must give one value: a join giving two is reported, never
;; guessed (the run stops with the host failure).
!(import! &self (library lib_tabling))
(= (both $a $b) (superpose ($a $b)))
!(tabled-lattice (twice $x) both)
(= (twice $x) 1)
(= (twice $x) 2)
!(collapse (twice q))
