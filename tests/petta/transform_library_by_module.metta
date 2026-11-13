; transform's library imported by a module: the importer holds the library's
; definitions, so its transforms, the module's functions and its own, run
; natively, as they would with the library imported directly.
!(bind! &s (new-space))
!(add-atom &s (n 1))
!(add-atom &s (n 2))
; control: before any import, transform names nothing and stays data
!(before-import (collapse (transform &s (n 1) found)))
!(import! &self fixtures/transform_library_module)
!(top-rows (sort-atom (collapse (transform &s (n $x) (got $x)))))
!(module-function-rows (sort-atom (collapse (module-rows &s))))
(= (own-rows $space) (transform $space (n $x) (own $x)))
!(own-function-rows (sort-atom (collapse (own-rows &s))))
; a later import into another space leaves them native
!(bind! &raw (new-space))
!(import! &raw fixtures/transform_library_module_data)
!(rows-after-data-import (sort-atom (collapse (transform &raw (edge $a $b) (e $a $b)))))
!(takes-into-space (collapse (module-takes &s)))
!(left (sort-atom (collapse (get-atoms &s))))
