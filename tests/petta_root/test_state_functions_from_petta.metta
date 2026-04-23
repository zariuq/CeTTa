; Ported from /home/zar/claude/hyperon/PeTTa/test_state_functions.metta
; Root-style Petta witness: run with launch CWD at the repo root.

!(import! &self lib/lib_zar)

!(bind! &counter (new-state 0))
!(assertEqual (get-state &counter) 0)
!(adjust-state-additive &counter 5)
!(assertEqual (get-state &counter) 5)
