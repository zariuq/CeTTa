; Entered collection, commitment and recovery scopes survive foreign dispatch.
!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/late_foreign_continuation.pl"))))
(= (receiver_prefix $x)
   (collapse (superpose
      ((Before a)
       (progn (import_prolog_function cold_dif)
          (let $f cold_dif
             (let $r ($f $x)
                (let $x (superpose (1 2 2)) (After $x)))))
       (Tail a)))))
!(receiver_prefix $x)

(= (receiver_delayed $x)
   (collapse
      (progn (import_prolog_function cold_dif)
         (let $f cold_dif (let $r ($f $x) (Value $x))))))
!(collapse (let ((Value $y)) (receiver_delayed $x)
               (let $y 1 forbidden)))
!(collapse (let ((Value $y)) (receiver_delayed $x)
               (let $x 1 (let $y 2 (Pair $x $y)))))

(= (receiver_nested_once $x)
   (once
      (let $r
         (once
            (progn (import_prolog_function cold_dif)
               (let $f cold_dif
                  (let $u ($f $x)
                     (let $x (superpose (1 2 3)) (Inner $x))))))
         (let $s (superpose (a b)) (Outer $r $s)))))
!(collapse (receiver_nested_once $x))

(= (receiver_equation_cut $x)
   (progn (import_prolog_function cold_dif)
      (let $f cold_dif
         (let $u ($f $x)
            (let $x (superpose (1 2 3))
               (let $committed (cut) (Chosen $x)))))))
(= (receiver_equation_cut $x) (Fallback $x))
!(collapse (receiver_equation_cut $x))

(= (receiver_throw $x)
   (collapse (superpose
      ((Before a)
       (progn (import_prolog_function cold_dif)
          (let $f cold_dif
             (let $r ($f $x) (sqrt-math -1.0))))
       (progn (println! forbidden_receiver_tail) (Tail a))))))
!(catch (receiver_throw $x))
!(collapse (let $f receiver_throw ($f $x)))

(= (receiver_after_collect_cut $x)
   (let $items
      (collapse
         (progn (import_prolog_function cold_dif)
            (let $f cold_dif
               (let $r ($f $x)
                  (let $x (superpose (1 2)) (Kept $x))))))
      (let $u (cut)
         (let $k (superpose (a b)) (AfterCollection $items $k)))))
!(collapse (receiver_after_collect_cut $x))

; A later native error obeys the same entered collection boundary.
!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/late_foreign_continuation.pl"))))
(= (receiver_later_raise $x)
   (collapse (superpose
      ((progn (import_prolog_function cold_dif)
          (let $f cold_dif (let $r ($f $x) (Prefix $x))))
       (% 1 0)
       (progn (println! forbidden_native_tail) (Tail a))))))
!(catch (receiver_later_raise $x))
!(collapse (let $f receiver_later_raise ($f $x)))

(= (receiver_bad $x) (+ $x 1))
(= (receiver_inner_recover $x)
   (collapse (superpose
      ((progn (import_prolog_function cold_dif)
          (let $f cold_dif (let $r ($f $x) (Prefix $x))))
       (let $f receiver_bad ($f a))
       (Tail a)))))
!(receiver_inner_recover 2)
!(let $f receiver_inner_recover ($f 2))

(= (receiver_nested_recovery $x)
   (collapse (superpose
      ((OuterPrefix a)
       (let $f receiver_later_raise ($f $x))
       (OuterTail a)))))
!(receiver_nested_recovery $x)

; A tentative case match must drain awakened constraints before committing.
!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/late_foreign_continuation.pl"))))
!(import_prolog_function cold_dif)
!(collapse (let $r (cold_dif $x) (case $x ((1 first) (2 second)))))
!(collapse (let $r (cold_dif $x) (case (Key $x) (((Key 1) first) ((Key 2) second)))))

; Nested receivers and future or entered matches retain delayed constraints.
!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/late_foreign_continuation.pl"))))
(Selected 1)
(Selected 2)
(= (receiver_nested_owned $x)
   (collapse (superpose
      ((OuterPrefix a)
       (let $r (collapse
         (progn (import_prolog_function cold_dif)
            (let $f cold_dif (let $u ($f $x) (Inner $x)))))
          (Outer $r))
       (OuterTail a)))))
!(receiver_nested_owned 2)
!(collapse (let ((OuterPrefix a) (Outer ((Inner $y))) (OuterTail a))
               (receiver_nested_owned $x) (let $y 1 forbidden)))
!(collapse (let ((OuterPrefix a) (Outer ((Inner $y))) (OuterTail a))
               (receiver_nested_owned $x) (let $x 1 (let $y 2 (Pair $x $y)))))

(= (receiver_nested_raise $x)
   (collapse (superpose
      ((OuterPrefix a)
       (collapse (superpose
          ((InnerPrefix a)
           (progn (import_prolog_function cold_dif)
              (let $f cold_dif (let $u ($f $x) (sqrt-math -1.0))))
           (progn (println! forbidden_inner_tail) (InnerTail a)))))
       (progn (println! forbidden_outer_tail) (OuterTail a))))))
!(catch (receiver_nested_raise $x))
!(collapse (let $f receiver_nested_raise ($f $x)))

(= (receiver_future_match $x)
   (progn (import_prolog_function cold_dif)
      (let $f cold_dif
         (let $u ($f $x) (match &self (Selected $x) (Good $x))))))
!(collapse (receiver_future_match $x))

(= (receiver_entered_match $x)
   (match &self (Selected $x)
      (progn (import_prolog_function cold_dif)
         (let $f cold_dif (let $u ($f $x) (Good $x))))))
!(collapse (receiver_entered_match $x))
