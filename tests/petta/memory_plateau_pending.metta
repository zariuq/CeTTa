;; Shapes of the agent turn (agent_turn.metta) that do not plateau yet, each
;; measured in bytes the allocator holds per turn after a warm-up window of N
;; turns. A plateau reads (close to) 0 here; memory_plateau.metta holds the
;; shapes that already do to it.
;;
;; Usage: cetta --profile extended --lang petta memory_plateau_pending.metta N
!(import! &self agent_turn)
;; One reading, deterministic: a match left open as a choice point would keep
;; everything a form allocates after it until the form ends.
(= (heap) (car-atom (collapse (gauge heap-in-use-bytes))))
(= (fresh-fact $k)
   (let* (($payload (format-args "payload {}" ($k)))
          ($a (add-atom &memory (fresh $k $payload))))
     (remove-atom &memory (fresh $k $payload))))
(= (fresh-space $k) (let $s (new-space) (add-atom $s (row $k))))
(= (rule-churn $k)
   (let $a (add-atom &self (= (memo) remembered))
     (remove-atom &self (= (memo) remembered))))
(= (turns-and $extra $k $end)
   (if (>= $k $end)
       done
       (let* (($t (turn $k)) ($x ($extra $k))) (turns-and $extra (+ $k 1) $end))))
(= (per-turn $from $to)
   (/ (- (get-state &heap-2) (get-state &heap-1)) (- $to $from)))
!(let $c (counts) settled)
!(turns 0 (argv 1))
;; A fact that differs in every turn, added and removed again: the term
;; universe keeps the removed term.
!(turns-and fresh-fact 0 (argv 1))
!(change-state! &heap-1 (heap))
!(turns-and fresh-fact (argv 1) (* 3 (argv 1)))
!(change-state! &heap-2 (heap))
!(fresh-facts (per-turn (argv 1) (* 3 (argv 1))))
;; A space made in every turn, written once and dropped: spaces live until
;; teardown, and the first row allocates the exact-membership index whole.
!(turns-and fresh-space 0 (argv 1))
!(change-state! &heap-1 (heap))
!(turns-and fresh-space (argv 1) (* 3 (argv 1)))
!(change-state! &heap-2 (heap))
!(fresh-spaces (per-turn (argv 1) (* 3 (argv 1))))
;; An equation added and removed again: the program keeps what it compiled
;; for each of its versions.
!(turns-and rule-churn 0 (argv 1))
!(change-state! &heap-1 (heap))
!(turns-and rule-churn (argv 1) (* 3 (argv 1)))
!(change-state! &heap-2 (heap))
!(rules (per-turn (argv 1) (* 3 (argv 1))))
;; The turns as one tail-recursive top-level evaluation, so the cleanup at
;; the end of a form cannot hide growth: the heap is read after the first
;; window and at the end.
(= (one-form $k $end $h1)
   (if (>= $k $end)
       (one-form-evaluation (/ (- (heap) $h1) (- $end (argv 1))))
       (let* (($h (if (== $k (argv 1)) (heap) $h1))
              ($t (turn $k)))
         (one-form (+ $k 1) $end $h))))
!(one-form 0 (* 3 (argv 1)) 0)
