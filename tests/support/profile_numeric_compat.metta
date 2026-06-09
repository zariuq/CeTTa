; he-compat follows Rust HE numeric behavior for integer division and rationals.

!(/ 7 3)
!(/ -7 3)
!(/ 7 -3)
!(/ -7 -3)

!(+ 999/1000 999/1000)
!(numeric-eq 1/2 0.5)
!(< 1/2 1)
!(== 1/2 2/4)

!(bind! &ratios (new-space))
!(add-atom &ratios (ratio 2/4))
!(match &ratios (ratio $x) $x)
!(match &ratios (ratio 1/2) ok)

!(// 7 3)
!(get-type //)
!(min-atom (1/2 2/3 3/4))
