!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_read_certificate.pl"
    (cetta_decode_nested_list cetta_decode_graph_list
     cetta_decode_open_list cetta_decode_alias_pair))

; A proper outer list can contain a cyclic head.
!(== (car-atom (cetta_decode_graph_list))
     (car-atom (cetta_decode_graph_list)))
; Open tails and duplicated variables retain their identities.
!(let (a $x) (cetta_decode_open_list) $x)
!(let ($x $x) (cetta_decode_alias_pair) (let $x linked $x))
!(size-atom (cetta_decode_nested_list 384))
!(collapse
   (let ($x $y) (cetta_decode_alias_pair)
     (let $x first (let $y second inconsistent))))
; A new foreign call starts with no certificate from the previous result.
!(let $_ (cetta_decode_nested_list 32)
   (== (car-atom (cetta_decode_graph_list))
       (car-atom (cetta_decode_graph_list))))
; Resuming one producer can cross from trees to graphs and back to trees.
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_read_resumption.pl"
    (cetta_decode_mixed_answer))
!(collapse
   (let $value (cetta_decode_mixed_answer)
     (let $head (car-atom $value)
       (if (== $head plain) early
           (if (== $head last) late cyclic)))))
