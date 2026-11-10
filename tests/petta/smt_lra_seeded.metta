; Seeded problems in linear real arithmetic: reals x0 .. x3, bound atoms
; over linear forms of one to three terms with coefficients -3 .. 3, bounds
; -6 .. 6 and the four relations, clauses of one to three literals.  Each
; line is a problem's seed and the smt library's verdicts on its clauses
; and on its SMT-LIB text (sat, or the refutation).  With SMT_DIR set to a
; directory the problems are also written there in SMT-LIB (QF_LRA), one
; file per seed, for a reference solver to answer.
!(import! &self smt)
!(import! &self fs)
!(import! &self str)
!(import! &self system)

!(import! &self "fixtures/smt_lra_generator.metta")

(= (problem $seed)
   (let* ((($cs $text) (clauses (next $seed) (+ 4 (% $seed 9)) () ""))
          ($dir (system:getenv-or-default "SMT_DIR" ""))
          ($_w (if (== $dir "") ()
                 (fs:write (str:concat $dir (str:concat "/" (str:concat (repr $seed) ".smt2")))
                           (str:concat (header) (str:concat $text "(check-sat)\n"))))))
     ($seed (car-atom (smt:solve $cs))
            (smt:solve-text (str:concat (header) (str:concat $text "(check-sat)\n"))))))

(= (problems $seed $n)
   (if (== $n 0) ()
     (let $_ (println! (problem $seed))
       (problems (next (next (next (next (next (next $seed)))))) (- $n 1)))))

!(problems 7 200)
