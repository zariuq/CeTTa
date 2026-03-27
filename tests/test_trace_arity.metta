; HE debug.rs: trace! expects exactly two atoms.

!(assertEqualToResult (trace! 1) ((Error (trace! 1) IncorrectNumberOfArguments)))
!(assertEqualToResult (trace! 1 2 3) ((Error (trace! 1 2 3) IncorrectNumberOfArguments)))
