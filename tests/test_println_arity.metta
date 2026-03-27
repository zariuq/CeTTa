; HE string.rs: println! expects exactly one atom.
!(assertEqualToResult (println! A B) ((Error (println! A B) IncorrectNumberOfArguments)))
