; Minimal boolean comparison relation sibling for Petta parity.

!(test (#= 42 42)
       True)

!(test (#= 42 7)
       False)

!(test (#= $X 42)
       True)

!(test (let (#= $X 42) True $X)
       42)

!(test (let (#= 7 $Y) True $Y)
       7)

!(test (let True (#= (#+ 40 2) 42) ok)
       ok)

!(test (collapse (let True (#= (#+ $X 2) 42) $X))
       (40))
