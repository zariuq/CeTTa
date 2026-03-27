; HE debug.rs: print-alternatives! expects exactly two arguments.
!(assertEqualToResult (print-alternatives! "alts") ((Error (print-alternatives! "alts") IncorrectNumberOfArguments)))
