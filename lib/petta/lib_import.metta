; Native PeTTa package-import capability.
; Importing this module enables the explicit git-import! effect; relational
; evaluation and search remain in the native PeTTa machine.

!(import_prolog_function consult)
!(import_prolog_function use_module)

; Foreign files are loaded by the optional libpl boundary.  The definitions
; remain ordinary PeTTa relations so source evaluation, nondeterminism, and
; predicate registration stay visible to the native search machine.
(= (import_prolog_functions_from_file $source $heads)
   (let $source (exists_file)
      (let $source (consult)
         (let $head (superpose $heads)
            (import_prolog_function $head)))))

(= (import_prolog_functions_from_module $source $heads)
   (let $source (exists_file)
      (let $source (use_module)
         (let $head (superpose $heads)
            (import_prolog_function $head)))))

(: git-import! (-> Atom Bool))
(: git-import! (-> Atom Atom Bool))
(: git-import! (-> Atom Atom Atom Bool))
