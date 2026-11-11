; Seeded problems in the theory of equality with uninterpreted functions:
; constants a b c d, a unary function f, a binary function g and a unary
; predicate p, terms nested twice at most, clauses of one to three literals,
; each an equality or the predicate, negated or not.  Each line is a problem's
; seed and the smt library's verdicts on its clauses and on its SMT-LIB text
; (sat, or the refutation).  With SMT_DIR set to a directory the
; problems are also written there in SMT-LIB (QF_UF), one file per seed, for a
; reference solver to answer.
!(import! &self smt)
!(import! &self fs)
!(import! &self str)
!(import! &self system)

(= (next $s) (% (* $s 48271) 2147483647))
(= (pick $s $xs) (index-atom $xs (% $s (size-atom $xs))))

(= (term $s $d)
   (let $k (% $s 3)
     (if (or (== $d 0) (== $k 0)) (pick (next $s) (a b c d))
       (if (== $k 1) (f (term (next $s) (- $d 1)))
         (g (term (next $s) (- $d 1)) (term (next (next $s)) (- $d 1)))))))

(= (atom-of $s)
   (if (< (% $s 10) 7)
       (eq (term (next $s) 2) (term (next (next $s)) 2))
       (p (term (next $s) 2))))

; a clause as the library reads it, and its SMT-LIB text
(= (literals $s $n $pos $neg $text)
   (if (== $n 0) ((clause $pos $neg) $text)
     (let* (($a (atom-of $s))
            ($t (atom-text $a))
            ($positive (== (% (next (next (next $s))) 2) 0)))
       (if $positive
           (literals (next (next (next (next $s)))) (- $n 1) (cons-atom $a $pos) $neg
                     (str:concat $text (str:concat " " $t)))
           (literals (next (next (next (next $s)))) (- $n 1) $pos (cons-atom $a $neg)
                     (str:concat $text (str:concat " (not " (str:concat $t ")"))))))))

(= (atom-text (eq $x $y)) (str:concat "(= " (str:concat (repr $x) (str:concat " " (str:concat (repr $y) ")")))))
(= (atom-text (p $x)) (str:concat "(p " (str:concat (repr $x) ")")))

(= (clauses $s $n $acc $text)
   (if (== $n 0) ($acc $text)
     (let ($c $t) (literals $s (+ 1 (% (next $s) 3)) () () "")
       (clauses (next (next (next (next (next $s))))) (- $n 1) (cons-atom $c $acc)
                (str:concat $text (str:concat "(assert (or" (str:concat $t "))\n")))))))

(= (header) "(set-logic QF_UF)\n(declare-sort U 0)\n(declare-fun a () U)\n(declare-fun b () U)\n(declare-fun c () U)\n(declare-fun d () U)\n(declare-fun f (U) U)\n(declare-fun g (U U) U)\n(declare-fun p (U) Bool)\n")

(= (problem $seed)
   (let* ((($cs $text) (clauses (next $seed) (+ 6 (% $seed 9)) () ""))
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

!(problems 5 200)
