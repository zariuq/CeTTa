; Petta callable `empty` is a miss surface, not literal Empty data.

!(test (collapse (empty))
       ())

!(test (collapse (if False yes (empty)))
       ())

!(test (collapse (let True False (empty)))
       ())
