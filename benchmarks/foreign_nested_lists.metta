; Materialize nested foreign lists, then observe the outer list's size.
!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_read_certificate.pl"
    (cetta_decode_nested_list))
(= (foreign-nested-lists $calls $depth)
   (if (> $calls 0)
       (let $value (cetta_decode_nested_list $depth)
         (+ (size-atom $value) (foreign-nested-lists (- $calls 1) $depth)))
       0))
!(foreign-nested-lists 1000 64)
