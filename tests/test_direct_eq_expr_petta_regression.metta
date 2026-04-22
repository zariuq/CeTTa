; Expression-position = should behave like boolean equality, not a declaration.

!(test (= 42 42)
       True)

!(test (= 42 7)
       False)

!(test (= (#+ 40 2) 42)
       True)
