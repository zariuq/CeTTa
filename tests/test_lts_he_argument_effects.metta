!(import! &self lts:he)

(: pick (-> Symbol))
(= (pick) a)
(= (pick) b)
(= (same $x) (pair $x $x))
(: raw-same (-> Atom %Undefined%))
(= (raw-same $x) (pair $x $x))
(: hold-expr (-> Expression Atom))
(= (hold-expr $x) (quote $x))
(: typed-same (-> Symbol %Undefined%))
(= (typed-same $x) (pair $x $x))
(: overloaded (-> Number %Undefined%))
(: overloaded (-> Atom %Undefined%))
(= (overloaded $x) (quote $x))
(: prefer-raw (-> Atom %Undefined%))
(: prefer-raw (-> Symbol %Undefined%))
(= (prefer-raw $x) (quote $x))
(= (__cetta_lib_user-equation a) known-user-answer)

; Evaluated arguments choose before an equation duplicates its variable.
!(assertEqualToResult (same (pick)) ((pair a a) (pair b b)))
!(assertEqualToResult
  (collapse (lts:he:transitions (same (pick))))
  (((quote (same a)) (quote (same b)))))
!(assertEqualToResult
  (collapse (lts:he:trace-2 (same (pick))))
  ((((quote (same a)) (quote (pair a a)))
    ((quote (same b)) (quote (pair b b))))))
!(assertEqualToResult (pair (pick) (pick))
  ((pair a a) (pair a b) (pair b a) (pair b b)))
!(assertEqualToResult
  (collapse (lts:he:transitions (typed-same (pick))))
  (((quote (typed-same a)) (quote (typed-same b)))))

; Raw domains retain syntax, including effects and quoted expressions.
!(assertEqualToResult
  (collapse (lts:he:transitions (raw-same (pick))))
  (((quote (pair (pick) (pick))))))
!(assertEqualToResult
  (collapse (lts:he:transitions (hold-expr (println! held-print))))
  (((quote (quote (println! held-print))))))
!(assertEqualToResult
  (collapse (lts:he:transitions (overloaded (pick))))
  (((quote (quote (pick))))))
!(assertEqualToResult
  (collapse (lts:he:transitions (prefer-raw (pick))))
  (((quote (quote (pick))))))
!(assertEqualToResult
  (collapse (lts:he:transitions (quote (println! quoted-print))))
  (()))
!(assertEqualToResult
  (collapse (lts:he:transitions (__cetta_lib_unavailable-operation a)))
  (()))
!(assertEqualToResult
  (collapse (lts:he:transitions (__cetta_lib_user-equation a)))
  (((quote known-user-answer))))
!(assertEqualToResult
  (collapse (lts:he:transitions (unknown-fn (pick))))
  (((quote (unknown-fn a)) (quote (unknown-fn b)))))

; A request identifies the pending operation and retains the whole context.
!(assertEqualToResult
  (collapse (lts:he:transitions (println! reflected-print)))
  (((quote (lts:he:request (quote (println! reflected-print))
                          (quote (println! reflected-print)))))))
!(assertEqualToResult
  (collapse (lts:he:transitions (same (println! nested-print))))
  (((quote (lts:he:request (quote (println! nested-print))
                          (quote (same (println! nested-print))))))))
!(assertEqualToResult
  (collapse (lts:he:transitions (chain (println! chain-print) $x (return $x))))
  (((quote (lts:he:request (quote (println! chain-print))
                          (quote (chain (println! chain-print) $x (return $x))))))))
!(assertEqualToResult
  (collapse (lts:he:transitions (let $x (println! let-print) $x)))
  (((quote (lts:he:request (quote (println! let-print))
                          (quote (let $x (println! let-print) $x)))))))
!(assertEqualToResult
  (collapse (lts:he:transitions (case (println! case-print) ((() ok)))))
  (((quote (lts:he:request (quote (println! case-print))
                          (quote (case (println! case-print) ((() ok)))))))))
!(assertEqualToResult
  (collapse (lts:he:step-state
    (quote (lts:he:request (quote (println! parked))
                           (quote (same (println! parked)))))))
  (()))

!(bind! &st (new-state 0))
!(assertEqualToResult
  (collapse (lts:he:transitions (change-state! &st 5)))
  (((quote (lts:he:request (quote (change-state! &st 5))
                          (quote (change-state! &st 5)))))))
!(assertEqualToResult (get-state &st) (0))
!(assertEqualToResult
  (collapse (lts:he:transitions (same (change-state! &st 7))))
  (((quote (lts:he:request (quote (change-state! &st 7))
                          (quote (same (change-state! &st 7))))))))
!(assertEqualToResult (get-state &st) (0))
!(assertEqualToResult
  (collapse (lts:he:transitions (add-atom &self reflected-added)))
  (((quote (lts:he:request (quote (add-atom &self reflected-added))
                          (quote (add-atom &self reflected-added)))))))
!(assertEqualToResult (match &self reflected-added found) ())

; Normal execution remains responsible for actually performing effects.
!(assertEqualToResult (get-state (change-state! &st 11)) (11))
!(assertEqualToResult (get-state &st) (11))
