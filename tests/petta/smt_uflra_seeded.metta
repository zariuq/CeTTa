; Seeded problems in linear real arithmetic with uninterpreted functions
; (QF_UFLRA): reals x0 .. x3 and f, g from reals to reals, terms xK, (f xK),
; (g xK) and (f (+ xK 1)), atoms equalities of two reals, equalities of two
; terms, or bounds over sums of one or two terms with coefficients -2 .. 2
; and bounds -3 .. 3,
; clauses of one or two literals, eight to sixteen of them.  Each line is a seed and the smt
; library's verdict on the SMT-LIB text (sat, or the refutation by
; congruence, Farkas and RUP steps).  With SMT_DIR set the problems are also
; written there, one file per seed, for a reference solver to answer.
!(import! &self smt)
!(import! &self fs)
!(import! &self str)
!(import! &self system)
!(import! &self "fixtures/smt_lra_generator.metta")

(= (uterm $s)
   (let* (($x (pick (next $s) ("x0" "x1" "x2" "x3"))) ($k (% $s 5)))
     (if (== $k 0) (str:concat "(f " (str:concat $x ")"))
       (if (== $k 1) (str:concat "(g " (str:concat $x ")"))
         (if (== $k 2) (str:concat "(f (+ " (str:concat $x " 1))"))
           $x)))))
(= (ucoef $s) (let $c (- (% $s 5) 2) (if (== $c 0) 1 $c)))
(= (usum $s)
   (let* (($t1 (uterm $s)) ($c1 (ucoef (next $s))))
     (if (== (% (next (next $s)) 2) 0)
         (str:concat "(* " (str:concat (num-text $c1) (str:concat " " (str:concat $t1 ")"))))
         (let* (($t2 (uterm (next (next (next $s))))) ($c2 (ucoef (next (next (next (next $s)))))))
           (str:concat "(+ (* " (str:concat (num-text $c1) (str:concat " " (str:concat $t1
             (str:concat ") (* " (str:concat (num-text $c2) (str:concat " " (str:concat $t2 "))"))))))))))))
(= (uvar $s) (pick $s ("x0" "x1" "x2" "x3")))
(= (uatom $s)
   (if (== (% $s 4) 0)
       (str:concat "(= " (str:concat (uvar (next $s)) (str:concat " " (str:concat (uvar (next (next $s))) ")"))))
     (if (== (% $s 4) 1)
       (str:concat "(= " (str:concat (uterm (next $s)) (str:concat " " (str:concat (uterm (next (next $s))) ")"))))
       (let* (($b (- (% (next (next (next $s))) 7) 3))
              ($r (pick (next (next $s)) ("<=" "<" ">=" ">"))))
         (str:concat "(" (str:concat $r (str:concat " " (str:concat (usum (next $s)) (str:concat " " (str:concat (num-text $b) ")"))))))))))
(= (ulits $s $n $text)
   (if (== $n 0) $text
     (let $a (uatom $s)
       (ulits (next (next (next (next (next $s))))) (- $n 1)
              (str:concat $text (if (== (% (next (next (next (next $s)))) 2) 0) (str:concat " " $a)
                                    (str:concat " (not " (str:concat $a ")"))))))))
(= (uclauses $s $n $text)
   (if (== $n 0) $text
     (uclauses (next (next (next (next (next (next $s)))))) (- $n 1)
               (str:concat $text (str:concat "(assert (or" (str:concat (ulits $s (+ 1 (% (next $s) 2)) "") "))\n"))))))
(= (uheader) "(set-logic QF_UFLRA)\n(declare-fun x0 () Real)\n(declare-fun x1 () Real)\n(declare-fun x2 () Real)\n(declare-fun x3 () Real)\n(declare-fun f (Real) Real)\n(declare-fun g (Real) Real)\n")
(= (uproblem $seed)
   (let* (($text (str:concat (uheader) (str:concat (uclauses (next $seed) (+ 8 (% $seed 9)) "") "(check-sat)\n")))
          ($dir (system:getenv-or-default "SMT_DIR" ""))
          ($_w (if (== $dir "") () (fs:write (str:concat $dir (str:concat "/" (str:concat (repr $seed) ".smt2"))) $text)))
          ($r (smt:solve-text $text)))
     ($seed (if (== (get-metatype $r) Symbol) $r (car-atom $r)))))
(= (uproblems $seed $n)
   (if (== $n 0) ()
     (let $_ (println! (uproblem $seed))
       (uproblems (next (next (next (next (next (next $seed)))))) (- $n 1)))))
!(uproblems 23 200)
