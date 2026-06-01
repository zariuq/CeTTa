; PeTTa HE profile accepts a unary function symbol as a filter predicate.

(= (filterfun $x) (> $x 3))

!(assertEqual
  (filter-atom (1 2 3 4 5) filterfun)
  (4 5))
