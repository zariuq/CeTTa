;; Computed names bound in a registry (named states, named spaces) are counted
;; names: each lives exactly as long as the registry binding it. A global
;; binding never ends, since change-state! and bind! only create or replace
;; and a transaction does not roll named state back; a scoped registry ends,
;; and with it every name it bound.
(= (names) (match (runtime-stats!) (runtime-counter registered-name-count $n) $n))
(= (state-key $prefix $n) (atom_concat $prefix $n))
;; Each hyperpose branch keeps its own named state.
(= (worker-loop $n)
   (if (== $n 0) done
       (let $r (collapse (hyperpose
                 ((let $k (state-key worker_left_ $n)
                    (progn (change-state! $k 1) (get-state $k)))
                  (let $k (state-key worker_right_ $n)
                    (progn (change-state! $k 2) (get-state $k))))))
         (worker-loop (- $n 1)))))
;; One computed key, rebound many times.
(= (rebind-loop $n)
   (if (== $n 0) done
       (let $_ (change-state! (state-key rebound_ 1) $n)
         (rebind-loop (- $n 1)))))
!(let $_ (names) settled)
;; Each branch holds its own name while it runs; whether it also sees the
;; other's depends on how the two branches are scheduled.
(= (holds-own $held) (and (>= $held 1) (<= $held 2)))
!(let* (($c (names))
        ($inside (collapse (hyperpose
                   ((let $k (state-key worker_probe_ 1)
                      (progn (change-state! $k 1) (holds-own (- (names) $c))))
                    (let $k (state-key worker_probe_ 2)
                      (progn (change-state! $k 2) (holds-own (- (names) $c)))))))))
   (workers-hold $inside then-release (- (names) $c)))
!(let* (($c (names)) ($_ (worker-loop 100)))
   (workers-release (- (names) $c)))
!(let* (($c (names)) ($_ (rebind-loop 1000)))
   (rebinding-reuses (- (names) $c)))
!(let* (($c (names))
        ($k (state-key txn_kept_ 1))
        ($_ (collapse (transaction (progn (change-state! $k 7) (empty))))))
   (rollback-keeps (- (names) $c) (get-state $k)))
