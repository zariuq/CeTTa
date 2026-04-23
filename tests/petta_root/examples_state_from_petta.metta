; Ported from /home/zar/claude/hyperon/PeTTa/examples/state.metta
; Root-style Petta witness adapted to CeTTa's assertEqual surface.

!(bind! &state (new-state rest))
!(assertEqual (get-state &state) rest)
!(let $_ (change-state! &state active) ())
!(assertEqual (get-state &state) active)
