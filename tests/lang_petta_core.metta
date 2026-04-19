(= (petta-foo) (1 2 3))

(= (petta-uadd)
   (let* (($t1 (add-atom &self (petta-u69foo a)))
          ($t2 (add-atom &self (petta-u69foo b))))
      (match &self (petta-u69foo $x) (petta-u69bar $x))))

!(import! &self support/petta_import_walk/nested/driver)

!(assertEqual (collapse (superpose (petta-foo))) (1 2 3))
!(assertEqual (car-atom (petta-foo)) 1)
!(assertEqual (=alpha (petta-foo) (1 2 3)) true)
!(assertEqual (is-member (+ 1 1) (2 3)) true)
!(assertEqual (msort (collapse (superpose (3 1 2)))) (1 2 3))
!(assertEqual (sort-atom (b a b)) (a b b))
!(assertEqual (msort (collapse (petta-uadd))) ((petta-u69bar a) (petta-u69bar b)))
!(assertEqual (collapse
  (let $a (add-atom &petta_named_space (petta-hit 42))
    (match &petta_named_space (petta-hit $x) $x)))
  (42))
!(assertEqual (ancestor-import-value) 17)
