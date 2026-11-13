; Seeded lexicographic optimization in linear real arithmetic: the seeded
; problems, most boxed in -10 .. 10, with two objectives, each a linear form
; minimized or maximized.  Each line is a seed and the smt library's stages,
; ending at the first stage no model attains; with SMT_DIR the scripts are
; written there for a reference solver; with Z3_DIR each line also says
; whether Z3's objectives agree on every stage the library gives.
!(import! &self smt)
!(import! &self fs)
!(import! &self str)
!(import! &self system)
!(import! &self "fixtures/smt_lra_generator.metta")
!(import! &self "fixtures/smt_z3_objectives.metta")

(= (box $seed)
   (if (== (% $seed 4) 0) ""
     "(assert (and (>= x0 (- 10)) (>= x1 (- 10)) (>= x2 (- 10)) (>= x3 (- 10)) (<= x0 10) (<= x1 10) (<= x2 10) (<= x3 10)))\n"))

(= (stages $r)
   (if (== (get-metatype $r) Symbol) ($r)
     (if (== (car-atom $r) lexicographic)
         (sort-atom (collapse (let $sp (index-atom $r 1) (let $st (superpose $sp) (verdict $st)))))
         ((verdict $r)))))

; Z3's objectives, one verdict each
(= (z3-stages $out $maxes)
   (let* (($items (parse (str:concat "(" (str:concat $out ")")))))
     (if (== (car-atom $items) unsat) (unsat)
       (z3-each (cdr-atom (index-atom $items 1)) $maxes))))
(= (z3-each $objs $maxes)
   (if (== $objs ()) ()
     (let (($o $rest) ($m $ms)) ((decons-atom $objs) (decons-atom $maxes))
       (cons-atom (z3-value-verdict (index-atom $o 1) $m) (z3-each $rest $ms)))))
(= (agree-all $ours $theirs)
   (if (== $ours ()) True
     (let (($a $as) ($b $bs)) ((decons-atom $ours) (decons-atom $theirs))
       (if (agree $a $b) (agree-all $as $bs) False))))

(= (lex $seed)
   (let* ((($cs $text0) (clauses (next $seed) (+ 4 (% $seed 9)) () ""))
          ($text (str:concat $text0 (box $seed)))
          (($t1 $tt1) (form (next (next (next $seed))) (+ 1 (% $seed 3)) () ""))
          (($t2 $tt2) (form (next (next (next (next (next $seed))))) (+ 1 (% (next $seed) 3)) () ""))
          ($m1 (== (% $seed 2) 1))
          ($m2 (== (% (next $seed) 2) 1))
          ($script (str:join "" ((header) $text (if $m1 "(maximize " "(minimize ") $tt1 ")\n"
                                  (if $m2 "(maximize " "(minimize ") $tt2 ")\n(check-sat)\n(get-objectives)\n")))
          ($dir (system:getenv-or-default "SMT_DIR" ""))
          ($_w (if (== $dir "") () (fs:write (str:concat $dir (str:concat "/" (str:concat (repr $seed) ".smt2"))) $script)))
          ($ours (stages (smt:solve-text $script)))
          ($zdir (system:getenv-or-default "Z3_DIR" "")))
     (if (== $zdir "") ($seed $ours)
       (let $theirs (z3-stages (fs:read-text (str:concat $zdir (str:concat "/" (str:concat (repr $seed) ".out")))) ($m1 $m2))
         ($seed $ours (if (agree-all $ours $theirs) z3-agrees (z3 $theirs)))))))

(= (lexes $seed $n)
   (if (== $n 0) ()
     (let $_ (println! (lex $seed))
       (lexes (next (next (next (next (next (next $seed)))))) (- $n 1)))))

!(lexes 13 120)
