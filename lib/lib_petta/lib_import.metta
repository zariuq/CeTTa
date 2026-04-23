; Petta-facing compatibility layer over CeTTa's generic Prolog helpers.
; Keep this thin so Petta library reuse stays above the core evaluator seam.

!(import! &self lib_prolog)

(= (consult $prologfile)
   (prolog-consult! $prologfile))

(= (use_module $prologfile)
   (prolog-use-module! $prologfile))

(: consult_file (-> Atom (->)))
(= (consult_file $prologfile)
   (prolog-consult! $prologfile))

(: use_module_file (-> Atom (->)))
(= (use_module_file $prologfile)
   (prolog-use-module! $prologfile))

(= (import_prolog_functions_from_file $prologfile $importfunctions)
   (import-prolog-functions-from-file $prologfile $importfunctions))

(= (import_prolog_functions_from_module $prologfile $importfunctions)
   (import-prolog-functions-from-module $prologfile $importfunctions))

(= (import_prolog_functions_from_file_nocheck $prologfile $importfunctions)
   (import-prolog-functions-from-file $prologfile $importfunctions))

(= (import_prolog_functions_from_file_pred $prologfile $importfunctions)
   (import-prolog-functions-from-file $prologfile $importfunctions))

(= (import_prolog_functions_from_module_pred $prologfile $importfunctions)
   (import-prolog-functions-from-module $prologfile $importfunctions))
