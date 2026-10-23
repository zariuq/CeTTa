!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/libpl_read_images.pl"
    (cetta_image_one cetta_image_alias cetta_image_distinct cetta_image_nested
     cetta_image_open cetta_image_cycle cetta_image_answers
     cetta_image_data cetta_image_constrained cetta_image_distinct_data))
!(size-atom (cetta_image_one))
!(let ($left $right) (cetta_image_alias) (== $left $right))
!(let ($left $right) (cetta_image_distinct)
   (if (== $left $right) wrong separate))
!(let ($left $right) (cetta_image_nested)
   (== (car-atom $left) (car-atom $right)))
!(let ($_ $left $right) (cetta_image_open) (let $left shared $right))
!(let $value (cetta_image_cycle)
   (== (car-atom $value) (car-atom (cdr-atom $value))))
!(collapse (let $value (cetta_image_answers) (size-atom $value)))
!(collapse (let ($left $right) (cetta_image_data)
   (let $left (superpose (alpha beta)) ($left $right))))
!(let $first (cetta_image_data)
   (let $second (cetta_image_data)
     (let ($left $left) $first
       (let ($right $right) $second
         (let $left independent-left
           (let $right independent-right ($left $right)))))))
!(collapse (let ($left $right) (cetta_image_data)
   (let $left red (let $right blue inconsistent))))
!(collapse (let $value (cetta_image_data)
   (let $branch (superpose (a b))
     (let ($variable $variable) $value
       (let $variable $branch ($branch $value))))))
!(collapse (let ($left $right) (cetta_image_constrained)
   (let $left (superpose (allowed blocked other)) $right)))
; Equal shape does not authorize using another source's already-read image.
!(let $value (cetta_image_distinct_data $input) ($input $value))
