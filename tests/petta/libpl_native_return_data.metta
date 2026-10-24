!(import! &self (library lib_import))
!(import! &self str)
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_native_eval.pl" (eval-once))

!(== (eval-once (quote (__cetta_lib_str string->symbol "native-datum"))) native-datum)
!(== (eval-once (quote (__cetta_lib_str string->symbol "Empty"))) Empty)
!(== (eval-once (quote Empty)) Empty)
!(== (collapse (eval-once (quote (empty)))) ())
!(== (eval-once (quote (quote (function (return native-datum)))))
     (quote (function (return native-datum))))
!(== (eval-once (quote (quote (Error native datum)))) (quote (Error native datum)))
!(let $text "native-datum"
    (== (eval-once (quote (__cetta_lib_str string->symbol $text))) native-datum))
