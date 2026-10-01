; PeTTa's &mork is its MORK space: lib_mm2's operations reach it, and
; mm2-exec runs the steps of its exec rules; mm2-exec has no answer on any
; other space.
!(import! &self (library lib_mm2))
!(＋ (a b))
!(? (a $x) $x)
!(~> (a $x) (b $x))
!(? (b $y) $y)
!(mm2-exec 4 5)
!(mm2-exec &mork 1)
!(add-atom &mork (exec 0 (, (c $z)) (, (d $z))))
!(add-atom &mork (c 7))
!(mm2-exec &mork 1)
!(match &mork (d $w) $w)
!(collapse (get-atoms &mork))
