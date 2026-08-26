!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_native_eval.pl"
    (eval-once eval-caught eval-many libpl-double eval-foreign))
(= (libpl-equation-double $value) (+ $value $value))
(= (metta $text)
   (let $term (sread $text) (eval $term)))
!(println! (libpl-eval-result (eval-once (+ 20 22))))
!(println! (libpl-equation-result
             (eval-once (libpl-equation-double 21))))
!(println! (libpl-metta-result
             (eval-once (metta "(+ 20 22)"))))
!(println! (libpl-caught-result
             (eval-caught (metta "(+ 20 22)"))))
!(println! (libpl-eval-many-result (collapse (eval-many))))
!(println! (libpl-reentrant-result (eval-foreign)))
!(println! (libpl-dynamic-eval-result
             (eval-once (eval (sread "(+ 20 22)")))))
