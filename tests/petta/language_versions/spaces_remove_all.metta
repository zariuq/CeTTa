; remove-all-atoms from lib_spaces empties the space it is given.
!(import! &self (library lib_spaces))
(pair a b)
(= (f $x) 42)

!(remove-all-atoms &self)
!(test (collapse (get-atoms &self)) ())
