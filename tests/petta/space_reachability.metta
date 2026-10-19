;; The space root seam, traced in shadow: runtime-stats! counts the spaces the
;; evaluator created and keeps (space-tracked-count) and those that nothing
;; reachable from the global roots holds (space-shadow-unreached-count). The roots
;; are the registry's bindings and named states; a reached value reaches the
;; spaces it holds, a reached space the spaces its rows hold. Spaces form
;; cycles, so this is a trace: two spaces holding only each other are
;; unreached. Nothing is freed; a form still running, choice points, trail
;; entries and foreign code also hold values, so the count is not yet a
;; collector's verdict.
(= (gauge $name) (match (runtime-stats!) (runtime-counter $name $n) $n))
(= (spaces) ((gauge space-tracked-count) (gauge space-shadow-unreached-count)))
!(let $_ (spaces) settled)
!(empty-run (spaces))
;; A space stored into an older space is reached through that space's row.
!(bind! &older (new-space))
!(let $s (new-space) (add-atom &older (holds $s)))
!(stored-in-older (spaces))
;; Two spaces holding only each other.
!(let* (($a (new-space)) ($b (new-space)))
   (progn (add-atom $a (link $b)) (add-atom $b (link $a)) linked))
!(cycle-unreached (spaces))
;; A space kept in named state is reached through the state.
!(change-state! &kept (new-space))
!(state-reaches (spaces))
;; Spaces made and dropped in a loop.
(= (drop-loop $n) (if (== $n 0) done (let $s (new-space) (drop-loop (- $n 1)))))
!(drop-loop 200)
!(dropped-unreached (spaces))
;; A space held only by a closure kept in named state is reached through the
;; closure's captured values.
(= (adder) (let $s (new-space) (|-> ($x) (add-atom $s $x))))
!(change-state! &adder (adder))
!(closure-reaches (spaces))
;; Not roots yet, so a live space held only through them counts as
;; unreached: the bindings of the form still running,
!(let $s (new-space) (running-binding-unreached (spaces)))
;; and, inside a transaction, the registry outside it. The transaction's own
;; registry binds working copies, and named state stays outside, so there
;; every space reads as unreached except one a working copy's row holds,
;; though a rollback returns to them all.
!(bind! &outer (new-space))
!(let $s (new-space) (add-atom &outer (holds $s)))
!(transaction
   (let* (($r (remove-atom &outer (holds $x))) ($n (spaces)))
     (outer-unreached-in-transaction $n)))
!(removed-unreached-after-commit (spaces))
;; The trace runs between the answers of a query over a space, while the
;; query is suspended.
!(bind! &rows (new-space))
!(let* (($a (new-space)) ($b (new-space)))
   (progn (add-atom &rows (row 1 $a)) (add-atom &rows (row 2 $b)) rows))
!(collapse (let $n (match &rows (row $n $s) $n) (between-answers $n (spaces))))
;; A value can name a temporary space whose form has ended, here the
;; inventory runtime-stats! returned, kept in named state: the trace never
;; reads through such a pointer (under valgrind or ASan it would fault).
!(change-state! &stale (runtime-stats!))
!(stale-state-ignored (spaces))
