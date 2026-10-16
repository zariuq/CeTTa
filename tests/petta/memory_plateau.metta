;; A long-running agent's turn (agent_turn.metta), repeated.
;;
;; Usage: cetta --profile extended --lang petta memory_plateau.metta N
;; Four windows of N turns run as top-level forms. After the first (warm-up)
;; window, the live storage gauges must stay where they are: the symbol table,
;; managed spellings and counted names exactly, and the bytes the allocator
;; holds within 512 KiB over the remaining three windows (each reading of the
;; gauges itself adds a few KiB of terms).
;;
;; Shapes that do not plateau yet are measured by memory_plateau_pending.metta
;; instead: facts that differ in every turn and are removed again, a new space
;; per turn, and the turns run as one top-level evaluation. A function added
;; at run time whose fresh closures cross into Prolog is left out too (their
;; code stays registered).
!(import! &self agent_turn)
;; The first reading also names every runtime counter; read once to settle.
;; A reading of the heap is the first runtime-stats! of its own form, so the
;; inventory spaces earlier readings made (released when a form ends) are not
;; in it.
!(let $c (counts) settled)
!(turns 0 (argv 1))
!(change-state! &heap-1 (gauge heap-in-use-bytes))
!(change-state! &counts-1 (counts))
!(turns (argv 1) (* 2 (argv 1)))
!(turns (* 2 (argv 1)) (* 3 (argv 1)))
!(turns (* 3 (argv 1)) (* 4 (argv 1)))
!(change-state! &heap-2 (gauge heap-in-use-bytes))
!(change-state! &counts-2 (counts))
;; Where the allocator's live bytes cannot be read the gauge stays 0, which
;; would read as a plateau: the heap must have been measured.
!(let* (($growth (- (get-state &heap-2) (get-state &heap-1))))
   ((counts-flat (== (get-state &counts-2) (get-state &counts-1)))
    (heap-measured (> (get-state &heap-1) 0))
    (heap-within-512-KiB (<= $growth 524288))))
