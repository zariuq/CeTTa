; SWI-Prolog's own delayed goals and attributes, posted through
; translatePredicate (freeze/2, dif/2, when/2, put_attr/3): a binding wakes
; the goals that wait on it, once each and in the order they were posted, and
; they may refuse the binding; attributes persist on their variable across
; calls; a collection or a hyperpose branch runs under the goals posted
; before it, and engine calls between a posting and its binding leave the
; goals in place.  The answers and printed effects are SWI-PeTTa's.
(= (post $y) (let $t (#+ $y 1) (#< $t 5)))
(= (spin $n) (if (> $n 0) (let $v (translatePredicate (succ $n $m)) (let $w ($v $m) (spin (- $n 1)))) done))
!(let* (($r (#+ $x 35)) ($_ (translatePredicate (= $r 42)))) $x)
!(let* (($_ (translatePredicate (freeze $x (Predicate (writeln woke))))) ($x 1)) done)
!(let* (($_ (translatePredicate (dif $x 1))) ($x 1)) no)
!(let* (($_ (translatePredicate (dif $x 1))) ($x 2)) yes)
!(let* (($_ (translatePredicate (dif $x $y))) ($x $y)) no)
!(let* (($_ (translatePredicate (dif $x $y))) ($x a) ($y b)) yes)
!(let* (($_ (translatePredicate (freeze $x (Predicate (writeln one))))) ($_ (translatePredicate (freeze $x (Predicate (writeln two))))) ($x 1)) ok)
!(let* (($_ (translatePredicate (when (Predicate (nonvar $x)) (Predicate (writeln when-fired))))) ($x a)) ok)
!(sort-atom (collapse (let* (($_ (translatePredicate (dif $x 2))) ($x (superpose (1 2 3)))) $x)))
!(let* (($_ (translatePredicate (put_attr $x my 5))) ($_ (translatePredicate (get_attr $x my $v)))) $v)
!(let* (($_ (translatePredicate (put_attr $x my 5))) ($y $x) ($_ (translatePredicate (get_attr $y my $v)))) $v)
!(let* (($_ (translatePredicate (freeze $x (Predicate fail)))) ($x 1)) no)
!(let $_ (translatePredicate (put_attr $x my 5)) (let $_ (translatePredicate (get_attr $x my $v)) $v))
!(let* (($_ (translatePredicate (freeze $x (Predicate (writeln woke))))) ($_ (println! before)) ($_ (translatePredicate (= $x 1)))) done)
!(let* (($_ (translatePredicate (dif $x 1))) ($r (translatePredicate (= $x 1)))) $r)
!(let* (($_ (translatePredicate (dif $x 1))) ($r (translatePredicate (= $x 2)))) $x)
!(let* (($_ (translatePredicate (when (Predicate (nonvar $x)) (Predicate (writeln when-fired))))) ($_ (translatePredicate (= $x a)))) ok)
!(let* (($_ (translatePredicate (dif $x 2))) ($q (sort-atom (collapse (let $x (superpose (1 2 3)) $x))))) $q)
!(let* (($_ (translatePredicate (freeze $x (Predicate (writeln inner-woke))))) ($q (sort-atom (collapse (let $x (superpose (1 2)) $x))))) $q)
!(let* (($c (post $y)) ($_ (spin 20000)) ($y 2)) ($c $y))
!(let* (($c (post $y)) ($_ (spin 20000)) ($y 10)) ($c $y))
!(sort-atom (collapse (let* (($c (post $y)) ($z (superpose (a b))) ($_ (spin 10000)) ($y 2)) ($z $y))))
!(sort-atom (collapse (let* (($c (post $y)) ($_ (spin 10000)) ($y (superpose (1 2 3 4 5)))) $y)))
!(let* (($r (#+ $x 10)) ($q (sort-atom (collapse (let* (($_ (spin 5000)) ($x (superpose (1 2)))) $r)))) ($_ (spin 5000)) ($x 5)) ($q $r))
!(let* (($_ (translatePredicate (dif $x 1))) ($v (collapse (hyperpose ((let $x 1 a) (let $x 2 b)))))) $v)
!(let* (($_ (translatePredicate (dif $x 1))) ($v (hyperpose ((let $x 1 a) (let $x 2 b))))) ($v $x))
!(let* (($v (hyperpose ((let $_ (translatePredicate (dif $y 2)) ok)))) ($y 2)) $v)
!(let* (($v (hyperpose ((let $_ (translatePredicate (dif $y 2)) ok)))) ($y 3)) ($v $y))
