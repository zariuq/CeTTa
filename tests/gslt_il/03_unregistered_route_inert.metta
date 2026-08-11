(in &stage-0 (= ready done))
(route grow &stage-0 &stage-1)
(= (grow ready) mapped-ready)
(! (missing-route done))
