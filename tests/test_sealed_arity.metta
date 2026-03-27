; HE-backed negative surface: sealed reports IncorrectNumberOfArguments on
; wrong arity.

!(assertEqualToResult (sealed) ((Error (sealed) IncorrectNumberOfArguments)))
!(assertEqualToResult (sealed ()) ((Error (sealed ()) IncorrectNumberOfArguments)))
!(assertEqualToResult (sealed () A B) ((Error (sealed () A B) IncorrectNumberOfArguments)))
