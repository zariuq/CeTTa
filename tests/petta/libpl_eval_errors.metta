!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_eval_errors.pl"
    (libpl-error-identity libpl-invalid-arity libpl-caught-arity
     libpl-invalid-arithmetic libpl-caught-arithmetic libpl-error-data))
!(println! (prolog-arity (libpl-caught-arity)))
!(println! (prolog-arithmetic (libpl-caught-arithmetic)))
!(println! (native-arity
    (case (catch (libpl-invalid-arity))
      (((Error $formal $context) caught) ($other unexpected)))))
!(println! (native-arithmetic
    (case (catch (libpl-invalid-arithmetic))
      (((Error $formal $context) caught) ($other unexpected)))))
!(println! (error-data (libpl-error-data)))
!(println! (subsequent-call (libpl-error-identity 42)))
!(println! SURVIVED)
