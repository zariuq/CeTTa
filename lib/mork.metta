; CeTTa MORK-facing helper surface.
; Intentionally narrow and honest: ordinary space creation, querying, sizing,
; and stepping live on the generic CeTTa space surface. This library only keeps
; the genuinely MORK-specific ACT helpers.

(: mork:open-act (-> Atom SpaceType))
(: mork:open-act (-> Atom Atom SpaceType))
(: mork:dump! (-> SpaceType Atom (->)))
(: mork:load-act! (-> SpaceType Atom (->)))

(@doc mork:dump!
  (@desc "Canonical MORK-specific ACT helper. Compiles the current contents of a space into a MORK .act artifact at the given path. Ordinary MeTTa copy semantics are preserved by default through compiled companion metadata when duplicates are present, while ordered queue/stack semantics remain out of scope for this phase-1 ACT surface.")
  (@params (
    (@param "Space to compile into a .act artifact")
    (@param "Output ACT filename")))
  (@return "Unit on success"))

(@doc mork:load-act!
  (@desc "Canonical MORK-specific ACT helper for additive materialization. Loads atoms from a compiled .act artifact into an ordinary live target space through CeTTa's bridge load path. This preserves ordinary copy semantics, but it is not the fast attached compiled-query path.")
  (@params (
    (@param "Target space that will receive the materialized atoms")
    (@param "ACT filename to load from")))
  (@return "Unit on success"))

(@doc mork:open-act
  (@desc "Canonical MORK-specific ACT helper for attached compiled querying. Opens a compiled .act artifact as a read-mostly MORK-backed space. Ordinary match and size operations stay attached to the compiled trie, while the first mutation or full atom introspection transparently materializes the space into ordinary CeTTa storage. The one-argument form defaults to hash discipline; the two-argument form accepts an explicit unordered discipline such as atom or hash.")
  (@params (
    (@param "Optional unordered discipline")
    (@param "ACT filename to open")))
  (@return "Fresh MORK-backed space attached to compiled ACT data"))

(= (mork:open-act $path)
  (function
    (case (__cetta_lib_mork_space_open_act $path)
      (($result (return $result))))))

(= (mork:open-act $kind $path)
  (function
    (case (__cetta_lib_mork_space_open_act $kind $path)
      (($result (return $result))))))

(= (mork:dump! $space $path)
  (function
    (case (__cetta_lib_mork_space_dump_act $space $path)
      (($result (return $result))))))

(= (mork:load-act! $space $path)
  (function
    (case (__cetta_lib_mork_space_import_act $space $path)
      (($result (return $result))))))
