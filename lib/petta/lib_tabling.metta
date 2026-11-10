(= (injectPrologCode $code)
   (progn (translatePredicate (open_string $code $S))
          (translatePredicate (load_files user ((Predicate (stream $S)))))
          True))

(: tabled (-> Atom %Undefined%))
(= (tabled $call)
   (progn (translatePredicate (atomics_to_string (":- table '" (car-atom $call) "'/" (+ 1 (length (cdr-atom $call))) ".") $code))
          (injectPrologCode $code)))

; (tabled-lattice (p $x ...) JOIN): p tabled with the results for each
; instance of its call joined inside the fixpoint, JOIN min, max or a function
; of two arguments (SWI's mode-directed tabling, the result the predicate's
; last argument, a function's join its predicate with three).
(: tabled-lattice (-> Atom Atom %Undefined%))
(= (tabled-lattice $call $join)
   (progn (translatePredicate (atomics_to_string (":- table '" (car-atom $call) "'(" (tabled-lattice-holes (length (cdr-atom $call))) (tabled-lattice-mode $join) ").") $code))
          (injectPrologCode $code)))
(= (tabled-lattice-mode $join)
   (if (== $join min) min
     (if (== $join max) max
       (progn (translatePredicate (atomics_to_string ("lattice('" $join "'/3)") $text)) $text))))
(= (tabled-lattice-holes $n)
   (if (== $n 0) ""
     (let $rest (tabled-lattice-holes (- $n 1))
       (progn (translatePredicate (atomics_to_string ("_," $rest) $text)) $text))))
