!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_py_call.pl"
    (libpl_python_string))
!(println! (libpl-py-call-result
             (libpl_python_string "agent — bridge")))
