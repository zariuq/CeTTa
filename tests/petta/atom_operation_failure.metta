; PeTTa's list predicates return [] for a value that cannot be a list.
!(test (size-atom 5) ())
!(test (sort-atom 5) ())
!(test (unique-atom 5) ())
!(test (alpha-unique-atom 5) ())
!(test (min-atom 5) ())
!(test (max-atom 5) ())
!(test (intersection-atom 5 (a)) ())

; That [] is an ordinary output value, so sequencing continues.
!(test (chain (size-atom 5) $ignored survived) survived)

; An invalid index is different: the predicate has no answer.
!(test (index-atom (1 2 3) 5) ())
!(test (index-atom (1 2 3) invalid) ())
!(test (chain (index-atom (1 2 3) 5) $ignored survived) ())

; Positive contracts remain unchanged.
!(test (size-atom (a b c)) 3)
!(test (sort-atom (c a b a)) (a a b c))
!(test (index-atom (a b c) 1) b)
