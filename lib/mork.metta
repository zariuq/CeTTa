; CeTTa MORK-facing helper surface.
; Intentionally narrow and honest: this layer describes spaces currently backed
; by the imported PathMap/MORK matcher. It does not claim MM2 exec semantics.

(: mork-space-backend (-> SpaceType Atom))
(: mork-backed-space? (-> SpaceType Bool))
(: mork-space-spec (-> SpaceType Atom))

(@doc mork-space-backend
  (@desc "Returns the current CeTTa match backend attached to a space. Today this is the truthful way to ask whether a space is currently using the pathmap-imported MORK-backed matcher.")
  (@params (
    (@param "Space to inspect")))
  (@return "Backend symbol such as native-subst-tree or pathmap-imported"))

(@doc mork-backed-space?
  (@desc "Returns True exactly when the space currently uses CeTTa's pathmap-imported MORK-backed matcher. This is intentionally backend-sensitive and does not pretend to create or configure such a space by itself.")
  (@params (
    (@param "Space to inspect")))
  (@return "Bool indicating whether the current backend is pathmap-imported"))

(@doc mork-space-spec
  (@desc "Builds a descriptive packet for a space from its semantic discipline, current match backend, and capability packet. This is descriptive introspection, not a promise that the full discipline x engine core surface already exists.")
  (@params (
    (@param "Space to inspect")))
  (@return "Descriptor packet summarizing discipline, backend, and capabilities"))

(= (mork-space-backend $space)
  (function
    (chain (space-match-backend $space) $backend
      (return $backend))))

(= (mork-backed-space? $space)
  (function
    (chain (space-match-backend $space) $backend
      (chain (== $backend pathmap-imported) $is-mork
        (return $is-mork)))))

(= (mork-space-spec $space)
  (function
    (chain (space-kind $space) $kind
      (chain (space-match-backend $space) $backend
        (chain (space-capabilities $space) $caps
          (return
            (mork-space
              (discipline $kind)
              (match-backend $backend)
              (capabilities $caps))))))))
