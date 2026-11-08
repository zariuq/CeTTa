; Seeded problems in linear integer arithmetic (QF_LIA): the seeded clauses
; over x0 .. x3 as integers in -5 .. 5, with one equality over two of them
; whose coefficients share a factor, so parity matters.  Each line is a
; seed and the smt library's verdict (sat, or a refutation, a tree of
; integer splits over checked certificates).  With SMT_DIR the problems are
; also written there, one file per seed, for a reference solver to answer.
!(import! &self smt)
!(import! &self fs)
!(import! &self str)
!(import! &self system)
!(import! &self "fixtures/smt_lra_generator.metta")

(= (iheader) "(set-logic QF_LIA)\n(declare-fun x0 () Int)\n(declare-fun x1 () Int)\n(declare-fun x2 () Int)\n(declare-fun x3 () Int)\n(assert (and (>= x0 (- 5)) (>= x1 (- 5)) (>= x2 (- 5)) (>= x3 (- 5)) (<= x0 5) (<= x1 5) (<= x2 5) (<= x3 5)))\n")
(= (equality $s)
   (let* (($a (* 2 (+ 1 (% $s 3)))) ($b (* 2 (+ 1 (% (next $s) 2)))) ($c (- (% (next (next $s)) 9) 4))
          ($x (pick (next (next (next $s))) ("x0" "x1"))) ($y (pick (next (next (next (next $s)))) ("x2" "x3"))))
     (str:join "" ("(assert (= (+ (* " (repr $a) " " $x ") (* " (repr $b) " " $y ")) " (num-text $c) "))\n"))))
(= (iproblem $seed)
   (let* ((($cs $text) (clauses (next $seed) (+ 3 (% $seed 7)) () ""))
          ($script (str:join "" ((iheader) $text (if (== (% $seed 3) 0) "" (equality (next (next $seed)))) "(check-sat)\n")))
          ($dir (system:getenv-or-default "SMT_DIR" ""))
          ($_w (if (== $dir "") () (fs:write (str:concat $dir (str:concat "/" (str:concat (repr $seed) ".smt2"))) $script)))
          ($r (smt:solve-text $script)))
     ($seed (if (== (get-metatype $r) Symbol) $r (car-atom $r)))))
(= (iproblems $seed $n)
   (if (== $n 0) ()
     (let $_ (println! (iproblem $seed))
       (iproblems (next (next (next (next (next (next $seed)))))) (- $n 1)))))
!(iproblems 29 200)
