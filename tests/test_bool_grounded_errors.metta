; HE grounded boolean ops: wrong arity raises IncorrectNumberOfArguments, while
; grounded non-boolean inputs raise BadArgType with expected Bool.

!(assertEqualToResult (and True) ((Error (and True) IncorrectNumberOfArguments)))
!(assertEqualToResult (or True) ((Error (or True) IncorrectNumberOfArguments)))
!(assertEqualToResult (xor True) ((Error (xor True) IncorrectNumberOfArguments)))
!(assertEqualToResult (not True False) ((Error (not True False) IncorrectNumberOfArguments)))
!(assertEqualToResult (and True "S") ((Error (and True "S") (BadArgType 2 Bool String))))
!(assertEqualToResult (or True "S") ((Error (or True "S") (BadArgType 2 Bool String))))
!(assertEqualToResult (xor True "S") ((Error (xor True "S") (BadArgType 2 Bool String))))
!(assertEqualToResult (not "S") ((Error (not "S") (BadArgType 1 Bool String))))
