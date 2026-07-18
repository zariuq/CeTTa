; Internal authority helper: revisions change on every real space mutation and
; are read only when an explicit snapshot consumer asks for them.
!(bind! &revision-space (new-space))
!(assertEqual (_minimal-space-revision &revision-space) 0)
!(add-atom &revision-space (revision-probe one))
!(assertEqual (_minimal-space-revision &revision-space) 1)
!(remove-atom &revision-space (revision-probe one))
!(assertEqual (_minimal-space-revision &revision-space) 2)
!(assertEqual (_minimal-space-revision not-a-space)
  (Error (_minimal-space-revision not-a-space)
    (BadArgType 1 SpaceType Symbol)))
