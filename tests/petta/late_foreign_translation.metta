; Translation of computed code observes foreign registration as well as
; equation changes. Previously compiled source occurrences keep their roles.
!(import! &self (library lib_import))
(= (force-code $code) (eval $code))
(= (captured-before-registration) (libpl-double 21))
!(captured-before-registration)
!(force-code (quote (libpl-double 21)))
!(force-code (quote (box (libpl-double 21))))
!(import_prolog_functions_from_file "tests/petta/fixtures/libpl_native_eval.pl" (libpl-double))
!(captured-before-registration)
!(force-code (quote (libpl-double 21)))
!(force-code (quote (box (libpl-double 21))))
