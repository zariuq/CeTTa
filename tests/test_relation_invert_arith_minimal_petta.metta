; Tiny arithmetic frontier witness extracted from invertfunction.

!(test (#+ 7 35)
       42)

!(test (let (#+ $X 35) 42 $X)
       7)

!(test (let (#+ 7 $Z) 42 $Z)
       35)
