; Native PeTTa package-import capability.
; Importing this module enables the explicit git-import! effect and, where
; SWI-Prolog is embedded, static-import! and use-module!; relational
; evaluation and search remain in the native PeTTa machine.
;
; (static-import! Space File) loads File, relative to the working directory,
; from File.qlf, else File.pl compiled to it, else File.metta converted to
; both, as SWI-PeTTa's importer does, and adds each row of it to Space.

!(import_prolog_function consult)
!(import_prolog_function use_module)

; (use-module! Module) loads the SWI library Module, as the reference's
; lib_import.pl defines it.  It names no Prolog predicate, so a program may
; consult that file itself.
(= (use-module! $module)
   (let $library (Predicate (quote (library $module)))
      (callPredicate (Predicate (quote (use_module $library))))))

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
(: git-import! (-> Atom Atom Atom Atom Bool))
