; Predicate is a function: its argument is evaluated, and the value names the
; term =.. builds from it.
(= (double $x) (* 2 $x))
(friend a b)
(friend c d)

!(Predicate (f (+ 1 2)))
!(Predicate (cons f (a b)))
!(let $s myspace (Predicate (cons $s (x y))))
!(Predicate (foo))
!(Predicate (quote (+ 41 1)))
!(collapse (Predicate true))
!(collapse (Predicate (+ 1 2)))
!(catch (Predicate (1 2)))
!(catch (Predicate ($f a)))
!(catch (Predicate ("s" a)))

; The goal operations take the value of their argument, any term.
!(let $_ (assertzPredicate (Predicate (score alice (+ 1 2))))
    (collapse (callPredicate (Predicate (score alice 3)))))
!(let $_ (assertzPredicate (Predicate (weight box (double 21))))
    (collapse (callPredicate (Predicate (weight box 42)))))
!(collapse (callPredicate (Predicate (= $y done))))
!(collapse (callPredicate (Predicate (true))))
!(let $g (Predicate (succ 3 $n)) (let $_ (callPredicate $g) $n))
!(let $_ (assertzPredicate (Predicate (h world)))
    (retractPredicate (Predicate (quote (h world)))))
!(retractPredicate (Predicate (quote (h world))))
!(catch (callPredicate (Predicate (1))))
!(catch (assertzPredicate (Predicate (1))))
!(catch (assertzPredicate (Predicate (atom_length a 1))))
!(catch (translatePredicate (unknown-predicate a $x)))
!(catch (callPredicate (Predicate (unknown-predicate a $x))))

; A space is the predicate its name names.
!(let $v (&self friend $x $y) $v)
!(collapse (let $g (Predicate (&self friend $x $y))
    (let $_ (callPredicate $g) ($x $y))))
!(collapse (let $_ (translatePredicate (&self friend a $who)) $who))
!(collapse (let $_ (translatePredicate (catch (Predicate (&self missing $x)) $_ fail)) $x))

; Goals Prolog hands back name the program's module as user.
!(let $_ (translatePredicate (freeze $x (Predicate (writeln woke))))
    (let $_ (translatePredicate (frozen $x $g)) $g))
!(let $_ (assertzPredicate
          (Predicate (:- (Predicate (big $x)) (Predicate (quote (> $x 5))))))
    (let $_ (translatePredicate (freeze $y (Predicate (big $y))))
        (collapse (let $y (superpose (3 7)) $y))))
