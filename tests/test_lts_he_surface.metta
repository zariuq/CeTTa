!(import! &self lts:he)

(= (f a) b)
(= (f a) c)
(= (g $x) (h $x))
(= (h $x) (k $x))
(= (choose-path) left)
(= (choose-path) right)

!(assertEqualToResult
  (collapse (lts:he:transitions (f a)))
  (((quote b) (quote c))))

!(assertEqualToResult
  (collapse (lts:he:transitions (g a)))
  (((quote (h a)))))

!(assertEqualToResult
  (collapse (lts:he:step-state (quote (g a))))
  (((quote (h a)))))

!(assertEqual
  (collapse (lts:he:step-state (quote (g a))))
  (collapse (lts:transitions __cetta_lib_lts_he_transitions (quote (g a)))))

!(assertEqual
  (collapse (lts:he:transitions (g a)))
  (collapse (lts:transitions lts:he:step-state (quote (g a)))))

!(assertEqualToResult
  (collapse (lts:he:step-state x))
  (()))

!(assertEqualToResult
  (collapse (lts:he:step-state (g a)))
  (()))

!(assertEqual
  (collapse (lts:he:step-state (quote x)))
  (collapse (lts:transitions __cetta_lib_lts_he_transitions (quote x))))

!(assertEqual
  (collapse (lts:he:transitions x))
  (collapse (lts:transitions lts:he:step-state (quote x))))

!(assertEqualToResult
  (collapse (lts:trace-2 lts:he:step-state (quote (g a))))
  ((((quote (h a)) (quote (k a))))))

!(assertEqual
  (collapse (lts:he:trace-2 (g a)))
  (collapse (lts:trace-2 lts:he:step-state (quote (g a)))))

!(assertEqualToResult
  (collapse (lts:trace-2 lts:he:step-state (quote x)))
  (()))

!(assertEqualToResult
  (collapse (lts:he:transitions (chain (f a) $r (return $r))))
  (((quote (chain b $r (return $r))) (quote (chain c $r (return $r))))))

!(assertEqualToResult
  (collapse (lts:he:transitions (chain b $r (return $r))))
  (((quote (return b)))))

!(assertEqualToResult
  (collapse (lts:he:transitions (chain (g a) $r (return $r))))
  (((quote (chain (h a) $r (return $r))))))

!(assertEqualToResult
  (collapse (lts:he:transitions (let $x (f a) (wrap $x))))
  (((quote (let $x b (wrap $x))) (quote (let $x c (wrap $x))))))

!(assertEqualToResult
  (collapse (lts:he:transitions (let (pair $x $y) (pair a b) $x)))
  (((quote a))))

!(assertEqualToResult
  (collapse (lts:he:transitions (let* (($x a) ($y b)) (pair $x $y))))
  (((quote (let $x a (let* (($y b)) (pair $x $y)))))))

!(assertEqualToResult
  (collapse (lts:he:transitions (let* () done)))
  (((quote done))))

!(assertEqualToResult
  (collapse (lts:he:transitions (eval (g a))))
  (((quote (g a)))))

!(assertEqualToResult
  (collapse (lts:he:transitions (case (f a) ((b one) (c two)))))
  (((quote (case b ((b one) (c two)))) (quote (case c ((b one) (c two)))))))

!(assertEqualToResult
  (collapse (lts:he:trace-2 (case (f a) ((b one) (c two)))))
  ((((quote (case b ((b one) (c two)))) (quote one))
    ((quote (case c ((b one) (c two)))) (quote two)))))

!(assertEqual
  (collapse (lts:he:trace-2 (case (f a) ((b one) (c two)))))
  (collapse (lts:trace-2 lts:he:step-state (quote (case (f a) ((b one) (c two)))))))

!(assertEqualToResult
  (collapse (lts:he:transitions (switch z ((z two)))))
  (((quote two))))

!(assertEqualToResult
  (collapse (lts:he:transitions (switch (g a) (((g a) raw) ((h a) evald)))))
  (((quote raw))))

!(assertEqualToResult
  (collapse (lts:he:trace-2 (let $p (choose-path) done)))
  ((((quote (let $p left done)) (quote done))
    ((quote (let $p right done)) (quote done)))))

!(assertEqualToResult
  (collapse (lts:he:transitions x))
  (()))

!(assertEqualToResult
  (collapse (lts:he:transitions (unknown-fn a)))
  (()))

!(assertEqualToResult
  (lts:he:is-normal-form x)
  (True))

!(assertEqual
  (lts:he:is-normal-form x)
  (lts:is-normal-form lts:he:step-state (quote x)))

!(assertEqualToResult
  (lts:he:is-can-step x)
  (False))

!(assertEqual
  (lts:he:is-can-step x)
  (lts:is-can-step lts:he:step-state (quote x)))

!(assertEqualToResult
  (lts:he:is-normal-form (f a))
  (False))

!(assertEqual
  (lts:he:is-normal-form (f a))
  (lts:is-normal-form lts:he:step-state (quote (f a))))

!(assertEqualToResult
  (lts:he:is-can-step (g hello))
  (True))

!(assertEqual
  (lts:he:is-can-step (g hello))
  (lts:is-can-step lts:he:step-state (quote (g hello))))

!(assertEqualToResult
  (lts:he:transitions)
  ((Error (lts:he:transitions) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:he:step-state)
  ((Error (lts:he:step-state) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:he:trace-2)
  ((Error (lts:he:trace-2) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:he:transitions (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:he:transitions (Error boom msg))
  (lts:transitions lts:he:step-state (quote (Error boom msg))))

!(assertEqualToResult
  (lts:he:step-state (quote (Error boom msg)))
  ((Error boom msg)))

!(assertEqual
  (lts:he:step-state (quote (Error boom msg)))
  (lts:transitions __cetta_lib_lts_he_transitions (quote (Error boom msg))))

!(assertEqualToResult
  (lts:he:trace-2 (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:he:trace-2 (Error boom msg))
  (lts:trace-2 lts:he:step-state (quote (Error boom msg))))

!(assertEqualToResult
  (lts:trace-2 lts:he:step-state (quote (Error boom msg)))
  ((Error boom msg)))

!(assertEqualToResult
  (lts:he:is-normal-form (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:he:is-normal-form (Error boom msg))
  (lts:is-normal-form lts:he:step-state (quote (Error boom msg))))

!(assertEqualToResult
  (lts:he:is-can-step (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:he:is-can-step (Error boom msg))
  (lts:is-can-step lts:he:step-state (quote (Error boom msg))))

!(assertEqualToResult
  (lts:he:is-normal-form)
  ((Error (lts:he:is-normal-form) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:he:is-can-step)
  ((Error (lts:he:is-can-step) IncorrectNumberOfArguments)))
