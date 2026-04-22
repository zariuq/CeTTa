; Port of /home/zar/claude/hyperon/PeTTa/test_state_functions.metta
; onto CeTTa-local Petta libraries.

!(import! &self lib/lib_zar)

!(bind! &counter (new-state 0))
!(assertEqual (get-state &counter) 0)
!(adjust-state-additive &counter 5)
!(assertEqual (get-state &counter) 5)
