; PeTTa's lib_roman, unmodified: higher-order effect-polymorphic arrows.
!(import! &self (library lib_roman))
!(test (/=\ (cons 1 (cons 2 ())) (cons 2 ())) (2))
!(test (let $q (cons 2 ()) (\= (1 2) $q)) (1))
!(test (map-flat (|-> ($x) (+ $x 1)) (1 2)) (2 3))
!(test (fold-flat bool-or False (False True)) True)
!(test (if-error-number 5 0) 5)
