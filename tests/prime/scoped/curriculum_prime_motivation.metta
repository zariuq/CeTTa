(hol:profile core)

; Prime Motivation 01, the deterministic finite automaton, in the hol:
; language. The word is an inductive type of symbols; the transition
; function is defined by its equations; the runner recurses on the word while
; the state changes at every call, which is the accumulator class admitted
; through the scrutinee-first form; acceptance and the language predicate
; are definitions on top. Every answer below is computed by the kernel's
; definitions, and the state after a word is checked as a typed equality.

!(bind! &thy (let $profile-space (new-space) (let $_ (add-atom $profile-space (hol:profile core)) $profile-space)))
!(hol:inductive &thy symbol (u 0) (: zero symbol) (: one symbol))
!(hol:inductive &thy state (u 0) (: q0 state) (: q1 state) (: q2 state))
!(hol:inductive &thy word (u 0) (: wnil word) (: wcons (-> symbol word word)))
!(hol:inductive &thy answer (u 0) (: yes answer) (: no answer))

; The transition table, one definition per symbol: a definition inspects one
; position, so the table is split by the symbol first and the state second.
!(hol:define &thy step-zero (-> state state)
   (= (step-zero q0) q1)
   (= (step-zero q1) q1)
   (= (step-zero q2) q1))
!(hol:define &thy step-one (-> state state)
   (= (step-one q0) q0)
   (= (step-one q1) q2)
   (= (step-one q2) q0))
!(hol:define &thy dfa-step (-> state symbol state)
   (= (dfa-step $q zero) (step-zero $q))
   (= (dfa-step $q one) (step-one $q)))

; The runner: structural on the word, the state changing at every call.
!(hol:define &thy dfa-run (-> state word state)
   (= (dfa-run $q wnil) $q)
   (= (dfa-run $q (wcons $a $w)) (dfa-run (dfa-step $q $a) $w)))
!(hol:known-proposition &thy dfa-run~scrutinee-first)

!(hol:define &thy dfa-accepting (-> state answer)
   (= (dfa-accepting q0) no)
   (= (dfa-accepting q1) no)
   (= (dfa-accepting q2) yes))
!(hol:define &thy ends-in-01 (-> word answer)
   (= (ends-in-01 $w) (dfa-accepting (dfa-run q0 $w))))

; The runs of the source program, as typed equalities.
!(hol:eq &thy (dfa-run q0 (wcons one (wcons zero (wcons one wnil)))) q2)
!(hol:eq &thy (ends-in-01 (wcons one (wcons zero (wcons one wnil)))) yes)
!(hol:eq &thy (ends-in-01 (wcons zero (wcons one (wcons one wnil)))) no)
!(hol:eq &thy (ends-in-01 wnil) no)
!(hol:eq &thy (ends-in-01 (wcons zero (wcons one wnil))) yes)

; Two states converge after reading zero: decided by computation on both
; sides. An equation about every word is a theorem, not a computation.
!(hol:eq &thy (dfa-run q1 (wcons zero wnil)) (dfa-run q2 (wcons zero wnil)))
