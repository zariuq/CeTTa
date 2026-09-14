; Seven independent rule slots: every successful match writes a wide linear
; substitution, with no nonlinear equality or cycle interaction.
(= (linear-match (Linear $a $b $c $d $e $f $g)) ok)
(= (linear-run $n $a $b $c $d $e $f $g)
   (if (== $n 0)
       linear-done
       (let $_ (linear-match (Linear $a $b $c $d $e $f $g))
         (linear-run (- $n 1) $a $b $c $d $e $f $g))))

!(linear-run 50000 $a $b $c $d $e $f $g)
