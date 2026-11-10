; Seeded optimization in linear real arithmetic: the seeded problems' clauses
; with an objective, a linear form minimized (even seeds) or maximized (odd
; ones).  Each line is a seed and the smt library's answers by its clauses
; (smt:minimize, smt:maximize) and by its SMT-LIB text ((minimize T) or
; (maximize T)): (optimum V), (infimum V) or (supremum V) when V is only
; approached, unbounded, or unsat; every answer carries its certificate.
; With SMT_DIR the scripts are written there for a reference solver; with
; Z3_DIR each line also says whether Z3's objectives there (SEED.out) agree.
!(import! &self smt)
!(import! &self fs)
!(import! &self str)
!(import! &self system)
!(import! &self "fixtures/smt_lra_generator.metta")

!(import! &self "fixtures/smt_z3_objectives.metta")

(= (z3-verdict $out $max)
   (let* (($items (parse (str:concat "(" (str:concat $out ")"))))
          ($first (car-atom $items)))
     (if (== $first unsat) unsat
       (let* (($objs (index-atom $items 1)) ($value (index-atom (index-atom $objs 1) 1)))
         (if (has-oo $value) unbounded
           (if (has-epsilon $value)
               ((if $max supremum infimum) (z3-base $value))
               (optimum (z3-num $value))))))))
; every real within -10 .. 10 but on a quarter of the seeds, so most
; objectives are bounded
(= (box $seed)
   (if (== (% $seed 4) 0) (() "")
     ((union-atom (collapse (let $x (superpose (x0 x1 x2 x3)) (clause ((ge $x -10)) ())))
                  (collapse (let $x (superpose (x0 x1 x2 x3)) (clause ((le $x 10)) ()))))
      "(assert (and (>= x0 (- 10)) (>= x1 (- 10)) (>= x2 (- 10)) (>= x3 (- 10)) (<= x0 10) (<= x1 10) (<= x2 10) (<= x3 10)))\n")))

(= (omt $seed)
   (let* ((($cs0 $text0) (clauses (next $seed) (+ 4 (% $seed 9)) () ""))
          (($bcs $btext) (box $seed))
          ($cs (union-atom $cs0 $bcs))
          ($text (str:concat $text0 $btext))
          (($term $tt) (form (next (next (next $seed))) (+ 1 (% $seed 3)) () ""))
          ($max (== (% $seed 2) 1))
          ($script (str:concat (header) (str:concat $text
                     (str:concat (if $max "(maximize " "(minimize ") (str:concat $tt ")\n(check-sat)\n(get-objectives)\n")))))
          ($dir (system:getenv-or-default "SMT_DIR" ""))
          ($_w (if (== $dir "") () (fs:write (str:concat $dir (str:concat "/" (str:concat (repr $seed) ".smt2"))) $script)))
          ($ours (verdict (if $max (smt:maximize $cs $term) (smt:minimize $cs $term))))
          ($text-ours (verdict (smt:solve-text $script)))
          ($zdir (system:getenv-or-default "Z3_DIR" "")))
     (if (== $zdir "") ($seed $ours $text-ours)
       (let $theirs (z3-verdict (fs:read-text (str:concat $zdir (str:concat "/" (str:concat (repr $seed) ".out")))) $max)
         ($seed $ours $text-ours (if (agree $ours $theirs) z3-agrees (z3 $theirs)))))))

(= (omts $seed $n)
   (if (== $n 0) ()
     (let $_ (println! (omt $seed))
       (omts (next (next (next (next (next (next $seed)))))) (- $n 1)))))

!(omts 11 150)
