; Ground sequences retain both answer multiplicity and element order.
!(GuestSequence member (is-member 3 (1 3 2)))
!(GuestSequence missing (is-member 4 (1 3 2)))
!(GuestSequence duplicate (collapse (is-member 3 (3 2 3))))
!(GuestSequence empty (is-member 3 ()))
!(GuestSequence reverse (reverse (1 2 3)))
!(GuestSequence reverse-empty (reverse ()))
