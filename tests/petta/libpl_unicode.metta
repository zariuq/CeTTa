!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_unicode.pl"
    (cetta-unicode-echo cetta-unicode-atom
     cetta-unicode-compound cetta-žluťoučký))
!(import_prolog_function string_length)
!(import_prolog_function sub_string)
!(cetta-unicode-echo "Příliš žluťoučký kůň")
!(string_length "prefix…suffix")
!(sub_string "ab…cd" 3 $_ 0)
!(cetta-unicode-echo kůň)
!(cetta-unicode-atom)
!(cetta-unicode-compound)
!(cetta-žluťoučký)
