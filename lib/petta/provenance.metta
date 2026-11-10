; Why-provenance of derivations in rule programs; see lib/provenance.metta.
(= (provenance:supports $facts $rules $goals $bound)
   (__cetta_lib_provenance_supports $facts $rules $goals $bound))
(= (provenance:supports-each $facts $rules $groups $bound)
   (__cetta_lib_provenance_supports_each $facts $rules $groups $bound))
