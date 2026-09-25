; Prime Motivation 01, the deterministic finite automaton, in the set:
; language. The word is an inductive type of symbols; the transition
; function is defined by its equations; the runner recurses on the word while
; the state changes at every call, which is the accumulator class admitted
; through the scrutinee-first form; acceptance and the language predicate
; are definitions on top. Every answer below is computed by the kernel's
; definitions, and the state after a word is checked as a typed equality.

!(bind! &thy (new-space))
!(set:inductive &thy symbol (u 0) (: zero symbol) (: one symbol))
!(set:inductive &thy state (u 0) (: q0 state) (: q1 state) (: q2 state))
!(set:inductive &thy word (u 0) (: wnil word) (: wcons (-> symbol word word)))
!(set:inductive &thy answer (u 0) (: yes answer) (: no answer))

; The transition table, one definition per symbol: a definition inspects one
; position, so the table is split by the symbol first and the state second.
!(set:define &thy step-zero (-> state state)
   (= (step-zero q0) q1)
   (= (step-zero q1) q1)
   (= (step-zero q2) q1))
!(set:define &thy step-one (-> state state)
   (= (step-one q0) q0)
   (= (step-one q1) q2)
   (= (step-one q2) q0))
!(set:define &thy dfa-step (-> state symbol state)
   (= (dfa-step $q zero) (step-zero $q))
   (= (dfa-step $q one) (step-one $q)))

; The runner: structural on the word, the state changing at every call.
!(set:define &thy dfa-run (-> state word state)
   (= (dfa-run $q wnil) $q)
   (= (dfa-run $q (wcons $a $w)) (dfa-run (dfa-step $q $a) $w)))
!(set:known-proposition &thy dfa-run~scrutinee-first)

!(set:define &thy dfa-accepting (-> state answer)
   (= (dfa-accepting q0) no)
   (= (dfa-accepting q1) no)
   (= (dfa-accepting q2) yes))
!(set:define &thy ends-in-01 (-> word answer)
   (= (ends-in-01 $w) (dfa-accepting (dfa-run q0 $w))))

; The runs of the source program, as typed equalities.
!(set:eq &thy (dfa-run q0 (wcons one (wcons zero (wcons one wnil)))) q2)
!(set:eq &thy (ends-in-01 (wcons one (wcons zero (wcons one wnil)))) yes)
!(set:eq &thy (ends-in-01 (wcons zero (wcons one (wcons one wnil)))) no)
!(set:eq &thy (ends-in-01 wnil) no)
!(set:eq &thy (ends-in-01 (wcons zero (wcons one wnil))) yes)

; Two states converge after reading zero: decided by computation on both
; sides. An equation about every word is a theorem, not a computation.
!(set:eq &thy (dfa-run q1 (wcons zero wnil)) (dfa-run q2 (wcons zero wnil)))
