; import-as! reads a module's own definitions under the importer's prefix:
; references inside the module follow, the module's own spellings stay
; undefined, each prefix is its own import, and a repeat changes nothing.
!(import-as! &self t import_as_lib)
!(t:twice 21)
!(t:quad 3)
!(twice 2)
!(import-as! &self u: import_as_lib)
!(u:quad 1)
!(import-as! &self t import_as_lib)
!(collapse (t:twice 1))
!(import! &self import_as_lib)
!(twice 5)
!(import-as! &self n import_as_ns_lib)
!(n:succ 41)
!(import-as! &self m megalodon)
!(let (Megalodon.theory $th $auth) (m:new-theory egal)
   (let (Megalodon.profile $name $revision $primitives $reserved $identity)
        (m:profile $th) $name))
