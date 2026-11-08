; The seeded problems of linear real arithmetic the smt tests share: reals
; x0 .. x3, bound atoms over linear forms of one to three terms with
; coefficients -3 .. 3, bounds -6 .. 6 and the four relations, clauses of one
; to three literals; each as the smt library's clauses and as SMT-LIB text.
(= (next $s) (% (* $s 48271) 2147483647))
(= (pick $s $xs) (index-atom $xs (% $s (size-atom $xs))))
(= (coef $s) (- (% $s 7) 3))

; an SMT-LIB numeral, negatives as (- n)
(= (num-text $n) (if (< $n 0) (str:concat "(- " (str:concat (repr (- 0 $n)) ")")) (repr $n)))

; a linear term: the clause form's (mul C X) and its SMT-LIB text
(= (term $s) (let* (($c (coef $s)) ($c1 (if (== $c 0) 1 $c)) ($x (pick (next $s) (x0 x1 x2 x3))))
               ((mul $c1 $x) (str:concat "(* " (str:concat (num-text $c1) (str:concat " " (str:concat (repr $x) ")")))))))

; a sum of n terms; one term stands alone, as SMT-LIB's + takes two or more
(= (form $s $n $parts $text)
   (if (== $n 0)
       (if (== (size-atom $parts) 1) ((car-atom $parts) $text)
           ((cons-atom add $parts) (str:concat "(+ " (str:concat $text ")"))))
     (let ($p $t) (term $s)
       (form (next (next $s)) (- $n 1) (cons-atom $p $parts)
             (if (== $text "") $t (str:concat $text (str:concat " " $t)))))))

(= (atom-of $s)
   (let* ((($f $ft) (form (next $s) (+ 1 (% $s 3)) () ""))
          ($b (- (% (next (next (next $s))) 13) 6))
          ($r (pick (next (next $s)) ((le "<=") (lt "<") (ge ">=") (gt ">")))))
     (let ($rel $rt) $r
       (($rel $f $b) (str:concat "(" (str:concat $rt (str:concat " " (str:concat $ft (str:concat " " (str:concat (num-text $b) ")"))))))))))

; a clause as the library reads it, and its SMT-LIB text
(= (literals $s $n $pos $neg $text)
   (if (== $n 0) ((clause $pos $neg) $text)
     (let* ((($a $t) (atom-of $s))
            ($positive (== (% (next (next (next $s))) 2) 0)))
       (if $positive
           (literals (next (next (next (next $s)))) (- $n 1) (cons-atom $a $pos) $neg
                     (str:concat $text (str:concat " " $t)))
           (literals (next (next (next (next $s)))) (- $n 1) $pos (cons-atom $a $neg)
                     (str:concat $text (str:concat " (not " (str:concat $t ")"))))))))

(= (clauses $s $n $acc $text)
   (if (== $n 0) ($acc $text)
     (let ($c $t) (literals $s (+ 1 (% (next $s) 3)) () () "")
       (clauses (next (next (next (next (next $s))))) (- $n 1) (cons-atom $c $acc)
                (str:concat $text (str:concat "(assert (or" (str:concat $t "))\n")))))))

(= (header) "(set-logic QF_LRA)\n(declare-fun x0 () Real)\n(declare-fun x1 () Real)\n(declare-fun x2 () Real)\n(declare-fun x3 () Real)\n")

