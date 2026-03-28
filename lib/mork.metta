; CeTTa MORK-facing helper surface.
; Intentionally narrow and honest: this layer describes spaces currently backed
; by the imported PathMap/MORK matcher. It does not claim MM2 exec semantics.

(: mork-space-new (-> SpaceType))
(: mork-space-new (-> Atom SpaceType))
(: mork-space-use! (-> SpaceType (->)))
(: mork-space-engine (-> SpaceType Atom))
(: mork-space-backend (-> SpaceType Atom))
(: mork-backed-space? (-> SpaceType Bool))
(: mork-space-spec (-> SpaceType Atom))
(: mork-space-count (-> SpaceType Number))
(: mork-space-add! (-> SpaceType Atom (->)))
(: mork-space-add-nodup! (-> SpaceType Atom (->)))
(: mork-space-remove! (-> SpaceType Atom (->)))
(: mork-space-atoms (-> SpaceType Expression))
(: mork-space-query (-> SpaceType Atom Atom %Undefined%))
(: mork-space-snapshot (-> SpaceType SpaceType))
(: mork-space-from-atoms (-> Expression SpaceType))
(: mork-space-from-atoms (-> Atom Expression SpaceType))
(: mork-space-dump-act! (-> SpaceType Atom (->)))
(: mork-space-import-act! (-> SpaceType Atom (->)))

(@doc mork-space-new
  (@desc "Creates a fresh space and explicitly configures its match backend to CeTTa's imported PathMap/MORK lane. The zero-argument form creates an atom-disciplined space; the one-argument form accepts the same semantic disciplines as new-space, such as hash, queue, or stack.")
  (@params (
    (@param "Optional space discipline such as hash, queue, or stack")))
  (@return "Fresh space value configured for the imported MORK-backed matcher"))

(@doc mork-space-use!
  (@desc "Mutates an existing space so its exact CeTTa match backend becomes pathmap-imported. This is an honest bridge-facing helper: it configures imported matching, but it does not claim MM2 program execution or source/sink semantics.")
  (@params (
    (@param "Space to reconfigure for imported MORK-backed matching")))
  (@return "Unit on success"))

(@doc mork-space-engine
  (@desc "Returns the higher-level engine family attached to a space. Today native backends report native and pathmap-imported reports mork.")
  (@params (
    (@param "Space to inspect")))
  (@return "Engine-family symbol such as native or mork"))

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
  (@desc "Builds a descriptive packet for a space from its semantic discipline, configured engine family, exact match backend, and capability packet. This is descriptive introspection, not a claim that full MM2 program/context execution already exists in CeTTa.")
  (@params (
    (@param "Space to inspect")))
  (@return "Descriptor packet summarizing discipline, backend, and capabilities"))

(@doc mork-space-count
  (@desc "Returns the current number of items in a MORK-backed space using the structured-space length surface. This is the honest library count helper; it does not pretend to be proof search output cardinality or an MM2 program metric.")
  (@params (
    (@param "Space to inspect")))
  (@return "Current item count in the space"))

(@doc mork-space-add!
  (@desc "Adds one atom to a space that the programmer intends to use through the MORK-backed lane. This is a library convenience wrapper over CeTTa's ordinary mutable space surface, not a new semantic capability.")
  (@params (
    (@param "Target space")
    (@param "Atom to insert")))
  (@return "Unit on success"))

(@doc mork-space-add-nodup!
  (@desc "Adds one atom through the no-duplicate insertion surface. This is useful for MORK-backed spaces when the intended semantics are set-like at the CeTTa layer.")
  (@params (
    (@param "Target space")
    (@param "Atom to insert if absent")))
  (@return "Unit on success"))

(@doc mork-space-remove!
  (@desc "Removes one matching atom from a MORK-backed space through CeTTa's ordinary mutable space surface.")
  (@params (
    (@param "Target space")
    (@param "Atom to remove")))
  (@return "Unit on success"))

(@doc mork-space-atoms
  (@desc "Returns the current atoms stored in the target space. This is useful for small trusted examples and snapshots; it is not a recommendation to materialize huge working spaces.")
  (@params (
    (@param "Space to inspect")))
  (@return "Expression containing the current stored atoms"))

(@doc mork-space-query
  (@desc "Runs one ordinary CeTTa match query against the target space and returns the instantiated template results. This is the honest space-layer query helper for MORK-backed spaces.")
  (@params (
    (@param "Target space")
    (@param "Pattern to match")
    (@param "Template to instantiate from bindings")))
  (@return "All instantiated query results"))

(@doc mork-space-snapshot
  (@desc "Returns a fresh MORK-backed snapshot copy of the target space by materializing its current atoms into a new space of the same discipline. This is a durable space-layer copy for library use, not MM2 program execution or a hidden engine-level snapshot handle.")
  (@params (
    (@param "Space to snapshot")))
  (@return "Fresh MORK-backed snapshot copy"))

(@doc mork-space-from-atoms
  (@desc "Builds a fresh MORK-backed space and fills it from a tuple of atoms. The zero-argument discipline form uses the default atom space; the two-argument form accepts an explicit semantic discipline such as hash, queue, or stack.")
  (@params (
    (@param "Optional semantic discipline")
    (@param "Tuple of atoms to insert")))
  (@return "Fresh populated MORK-backed space"))

(@doc mork-space-dump-act!
  (@desc "Compiles the current contents of a space into a MORK .act artifact at the given path. This phase-1 compiled-space surface is intentionally strict: it requires an unordered space with no duplicate atoms, because the current ACT bridge artifact is set-like rather than queue-ordered or multiset-preserving.")
  (@params (
    (@param "Space to compile into a .act artifact")
    (@param "Output ACT filename")))
  (@return "Unit on success"))

(@doc mork-space-import-act!
  (@desc "Imports atoms from a compiled .act artifact directly into the target space through CeTTa's MORK bridge load path. This is additive import semantics, not hidden replacement; use a fresh target space when you want a clean compiled-space load.")
  (@params (
    (@param "Target space that will receive the imported atoms")
    (@param "ACT filename to import")))
  (@return "Unit on success"))

(= (mork-space-new)
  (function
    (chain (new-space) $space
      (chain (mork-space-use! $space) $_
        (return $space)))))

(= (mork-space-new $kind)
  (function
    (chain (new-space $kind) $space
      (chain (mork-space-use! $space) $_
        (return $space)))))

(= (mork-space-use! $space)
  (function
    (chain (space-set-match-backend! $space pathmap-imported) $unit
      (return $unit))))

(= (mork-space-engine $space)
  (function
    (chain (space-engine $space) $engine
      (return $engine))))

(= (mork-space-backend $space)
  (function
    (chain (space-match-backend $space) $backend
      (return $backend))))

(= (mork-backed-space? $space)
  (function
    (chain (mork-space-engine $space) $engine
      (chain (== $engine mork) $is-mork
        (return $is-mork)))))

(= (mork-space-spec $space)
  (function
    (chain (space-kind $space) $kind
      (chain (mork-space-engine $space) $engine
        (chain (space-match-backend $space) $backend
          (chain (space-capabilities $space) $caps
            (return
              (mork-space
                (discipline $kind)
                (engine $engine)
                (match-backend $backend)
                (capabilities $caps)))))))))

(= (mork-space-count $space)
  (function
    (chain (space-len $space) $len
      (return $len))))

(= (mork-space-add! $space $atom)
  (function
    (chain (add-atom $space $atom) $unit
      (return $unit))))

(= (mork-space-add-nodup! $space $atom)
  (function
    (chain (add-atom-nodup $space $atom) $unit
      (return $unit))))

(= (mork-space-remove! $space $atom)
  (function
    (chain (remove-atom $space $atom) $unit
      (return $unit))))

(= (mork-space-atoms $space)
  (function
    (chain (collapse (get-atoms $space)) $atoms
      (return $atoms))))

(= (mork-space-query $space $pattern $template)
  (function
    (chain (match $space $pattern $template) $result
      (return $result))))

(= (mork-space-snapshot $space)
  (function
    (chain (space-kind $space) $kind
      (chain (collapse (get-atoms $space)) $atoms
        (chain (mork-space-from-atoms $kind $atoms) $snap
          (return $snap))))))

(= (mork-space-from-atoms $atoms)
  (function
    (chain (mork-space-new) $space
      (chain (add-atoms $space $atoms) $_
        (return $space)))))

(= (mork-space-from-atoms $kind $atoms)
  (function
    (chain (mork-space-new $kind) $space
      (chain (add-atoms $space $atoms) $_
        (return $space)))))

(= (mork-space-dump-act! $space $path)
  (function
    (case (__cetta_lib_mork_space_dump_act $space $path)
      (($result (return $result))))))

(= (mork-space-import-act! $space $path)
  (function
    (case (__cetta_lib_mork_space_import_act $space $path)
      (($result (return $result))))))
