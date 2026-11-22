; HE's equality promotes numbers: (p 1) meets a stored (p 1.0), so the
; empty-only case sees a nonempty match, as the enumeration does.
!(bind! &s (new-space))
!(add-atom &s (p 1.0))
!(d3 (collapse (case (collapse (match &s (p 1) yes)) ((() none)))))
!(d3-enumerated (collapse (match &s (p 1) yes)))
!(d3-absent (collapse (case (collapse (match &s (p 7) yes)) ((() none)))))
