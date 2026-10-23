; Read nested foreign data with a shared logical variable in its leaves.
!(import! &self (library lib_import))
!(import_prolog_functions_from_file
    "tests/petta/fixtures/foreign_open_image.pl"
    (cetta_foreign_open_image))
(= (foreign-open-images $calls $depth)
   (if (> $calls 0)
       (let $image (cetta_foreign_open_image $depth)
         (+ (size-atom $image) (foreign-open-images (- $calls 1) $depth)))
       0))
!(foreign-open-images 1000 64)
