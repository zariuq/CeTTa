; Unlike the program space, this owned space has no imported dependencies.
; Its ground query can retain the counted binding snapshot directly.
!(bind! &owned-snapshot (new-space))
!(add-atom &owned-snapshot (owned alpha))
!(add-atom &owned-snapshot (owned alpha))
!(collapse (match &owned-snapshot (owned alpha) 9))
!(collapse (match &owned-snapshot (owned absent) 9))
