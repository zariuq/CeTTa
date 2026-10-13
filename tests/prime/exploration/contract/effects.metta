; Two kinds of effect, two laws.
;
; A state cell is branch-local: each alternative starts from its parent's
; value, a shared parameter performs its update once in its branch, and no
; update reaches the ambient cell without an explicit commit.  This is the
; branch-local store of the call-time-choice core (each alternative of
; Derives.altsCons starts from its parent's store; force_commits; a sibling's
; memo may not be pre-filled, prefill_drops_events).
;
; A space write is a world effect: it is threaded through backtracking, it is
; committed when it is performed, and a failing alternative does not undo it.
; `once` stops before the alternatives after its first answer, so their writes
; never happen.  This is the world of the delimited-commitment core
; (Machines/ScopedCommit: performed_effect_is_not_rolled_back,
; once_stops_before_losing_effect).

!(bind! &c (new-state 0))
(= (tick) (let $c (get-state &c) (let $u (change-state! &c (+ $c 1)) $c)))
(= (same $x) (Pair $x $x))
(= (konst $x $y) $x)

; 1. A shared parameter updates once; two occurrences update twice.
!(same (tick))
!(Pair (tick) (tick))
; 2. An unused parameter never updates.
!(konst A (tick))
; 3. Siblings start from the same parent value.
!(let $x (superpose (L R)) (Pair $x (tick)))
; 4. Nothing above reached the ambient cell.
!(get-state &c)

; 5. Space writes persist, including the write of a failing alternative;
;    `once` never runs the alternative after its answer.
(= (attempt) (let $u (add-atom &self (tried a)) (empty)))
(= (attempt) (let $u (add-atom &self (tried b)) done))
(= (attempt) (let $u (add-atom &self (tried c)) late))
!(once (attempt))
!(match &self (tried $x) $x)

; 6. A source with no answer runs once: a `let` over it does not compute it
;    again, whether the whole result or a prefix of it is observed.
(= (silent $tag) (let $u (add-atom &self (ran $tag)) (empty)))
(= (over $tag) (let $x (silent $tag) $x))
!(collapse (over whole))
!(select 2 (over prefix))
!(match &self (ran $x) $x)
