; A computed let pattern publishes its effect once before its answer.
(= (binder $x) (progn (println! binder-entered) (Ctor $x)))
(= (trial $x) (let (binder 7) (Ctor $x) $x))
!(trial $outside)
