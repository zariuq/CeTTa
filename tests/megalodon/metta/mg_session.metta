; Retained checking sessions over a borrowed DAG in a space: pause on a row
; bound, resume the exact pending suffix, exhaust a work budget (not a
; refusal), report refused and malformed rows as data, finish a closed root
; as a theorem, and rebase or revalidate after the space or the theory
; changed.
!(import! &self megalodon)
!(let $state (mg:new-theory-state custom) (bind! &th $state))
(= (T) (mg:use (get-state &th)))
(= (AUTH) (mg:authority (get-state &th)))
!(mg:parameter (AUTH) "A" (Tp.prop))
!(mg:parameter (AUTH) "B" (Tp.prop))
(= (ctx-A) (Megalodon.context 0 (types) (hypotheses (Tm.named "A"))))
(= (closed) (Megalodon.context 0 (types) (hypotheses)))
(= (A->A) (Tm.imp (Tm.named "A") (Tm.named "A")))

; Rows: 1 [A] |- A by hyp 0; 2 |- A -> A by proofLam of 1;
; 3 |- A by modus ponens of 2 and 4; 4 |- A by hyp (refused: no hypothesis).
; add-atom stores its atom as written, so rows are spelled out in full.
!(bind! &rows (new-space))
!(add-atom &rows (Megalodon.node 1 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses (Tm.named "A"))) (Tm.named "A")) (Megalodon.hyp 0) (parents) 0)))
!(add-atom &rows (Megalodon.node 2 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses)) (Tm.imp (Tm.named "A") (Tm.named "A"))) (Megalodon.proofLam (Tm.named "A")) (parents 1) 0)))
!(add-atom &rows (Megalodon.node 4 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses)) (Tm.named "A")) (Megalodon.hyp 0) (parents) 0)))
!(add-atom &rows (Megalodon.node 3 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses)) (Tm.named "A")) (Megalodon.proofApp (Tm.named "A")) (parents 2 4) 0)))

!(let $session (mg:session-begin (T) &rows) (bind! &s (new-state $session)))
(= (S) (get-state &s))

; Pause after one row; the report keeps the exact pending suffix.
!(mg:session-advance (S) &rows (1 2) 1 0)
!(mg:session-is-done (S))
!(mg:session-inspect (S))
!(mg:session-judgment (S) 1)
; Resume: the pending suffix only.
!(mg:session-resume (S) &rows 0 0)
!(mg:session-is-done (S))
!(mg:statement (mg:session-finish (S) 2 (A->A)))

; Lazy rows (mg:session-begin-mode THEORY SPACE lazy) state a formed
; judgment convertible to their rule's conclusion, the form lazy evidence
; uses; an exact session holds every row to the rule's normal-form judgment.
!(mg:define (T) "AA" (Tp.prop) (Tm.imp (Tm.named "A") (Tm.named "A")))
!(bind! &lazy-rows (new-space))
!(add-atom &lazy-rows (Megalodon.node 1 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses (Tm.named "A"))) (Tm.named "A")) (Megalodon.hyp 0) (parents) 0)))
!(add-atom &lazy-rows (Megalodon.node 2 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses)) (Tm.named "AA")) (Megalodon.proofLam (Tm.named "A")) (parents 1) 0)))
!(let $session (mg:session-begin-mode (T) &lazy-rows lazy) (bind! &ls (new-state $session)))
!(mg:session-advance (get-state &ls) &lazy-rows (1 2) 0 0)
!(mg:statement (mg:session-finish (get-state &ls) 2 (A->A)))
!(let $session (mg:session-begin-mode (T) &lazy-rows exact) (bind! &xs (new-state $session)))
!(mg:session-advance (get-state &xs) &lazy-rows (1 2) 0 0)

; A work budget that runs out is exhaustion, not refusal.
!(let $fresh (mg:session-begin (T) &rows) (bind! &e (new-state $fresh)))
!(mg:session-advance (get-state &e) &rows (1 2) 0 1)
!(mg:session-resume (get-state &e) &rows 0 0)
!(mg:session-is-done (get-state &e))

; A refused row is a report, not an error; an unchecked parent is
; malformed.  Both keep the last checkpoint.
!(let $other (mg:session-begin (T) &rows) (bind! &bad (new-state $other)))
!(mg:session-advance (get-state &bad) &rows (4 3) 0 0)
!(mg:session-advance (get-state &bad) &rows (3) 0 0)
!(mg:session-inspect (get-state &bad))
!(case (mg:try-session-finish (get-state &bad) 3 (Tm.named "A"))
   (((Error $call $why) (refused $why)) ($theorem (accepted (mg:statement $theorem)))))

; The space changed but no checked row did: rebase keeps the checkpoint.
!(add-atom &rows (Megalodon.node 5 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses)) (Tm.imp (Tm.named "A") (Tm.named "A"))) (Megalodon.known "A_imp_A") (parents) 0)))
!(mg:session-rebase (S) &rows)
; The theory changed outside the checkpoint's footprint: revalidation keeps it.
!(mg:withdraw (AUTH) "B")
!(mg:session-revalidate (S))
!(mg:statement (mg:session-finish (S) 2 (A->A)))
; A checked row changed in the space.  The checkpoint keeps what it
; checked, so finishing still gives the theorem; rebasing onto the changed
; space is refused.
!(remove-atom &rows (Megalodon.node 1 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses (Tm.named "A"))) (Tm.named "A")) (Megalodon.hyp 0) (parents) 0)))
!(add-atom &rows (Megalodon.node 1 (Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses (Tm.named "A") (Tm.named "A"))) (Tm.named "A")) (Megalodon.hyp 1) (parents) 0)))
!(case (mg:try-session-finish (S) 2 (A->A))
   (((Error $call $why) (refused $why)) ($theorem (accepted (mg:statement $theorem)))))
!(case (mg:try-session-rebase (S) &rows)
   (((Error $call $why) (refused $why)) ($done (accepted $done))))
; The theory changed inside the footprint: revalidation refuses.
!(mg:withdraw (AUTH) "A")
!(case (mg:try-session-revalidate (S))
   (((Error $call $why) (refused $why)) ($done (accepted $done))))
!(mg:session-cancel (S))
