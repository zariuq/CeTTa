; Minimal boolean comparison relation siblings for Petta parity.

!(test (#> 5 2)
       True)

!(test (#> 1 2)
       False)

!(test (#< 1 2)
       True)

!(test (#< 5 2)
       False)

!(test (let True (#> (#+ 40 2) 41) ok)
       ok)

!(test (let True (#< (#+ 1 2) 5) ok)
       ok)
