; NotReducible / unreduced expression behavior
!(assertEqualToResult (unknown-fn x) ((unknown-fn x)))
!(assertEqualToResult (superpose ()) ())
!(assertEqualToResult (also-unknown a b c) ((also-unknown a b c)))
