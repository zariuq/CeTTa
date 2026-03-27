; HE string.rs: format-args expects exactly two arguments.
!(assertEqualToResult (format-args "{} {}") ((Error (format-args "{} {}") IncorrectNumberOfArguments)))
