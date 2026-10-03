; Late foreign registration leaves the entered caller's remaining work live.
!(callPredicate (Predicate (quote (consult "tests/petta/fixtures/late_foreign_continuation.pl"))))
(= (cold_inner $x) (let $v (let $callee cold_watch ($callee $x)) (Inner $v $x)))
(= (cold_outer $x) (let $v (cold_inner $x) (Outer $v $x)))
!(cold_outer $free)
(= (cold_install $free)
   (let $before (cold_outer $free)
      (progn
         (println! installed-once)
         (import_prolog_function cold_watch)
         (let $value (cold_outer $free)
            (let $free 7 (Result $before $value $free))))))
!(cold_install $free)

(= (cold_reject $x) (let $r (let $callee cold_dif ($callee $x)) (Rejected $r $x)))
!(cold_reject $free)
(= (cold_install_dif $free)
   (let $before (cold_reject $free)
      (progn
         (println! dif-installed-once)
         (import_prolog_function cold_dif)
         (let $value (cold_reject $free)
            (let $free 1 (MustNotAppear $before $value))))))
!(collapse (cold_install_dif $free))
!(let $v (cold_reject $free) (let $free 2 $v))

; Older equation alternatives survive transfer and do not repeat the prefix.
(= (cold_choices $x) (Choice first $x))
(= (cold_choices $x) (Choice second $x))
(= (cold_retry $free)
   (let $choice (cold_choices $free)
      (let $value (cold_reject $free)
         (let $free 2 (Result $choice $value)))))
!(collapse (cold_retry $free))
