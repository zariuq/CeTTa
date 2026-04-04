; mork MM2 showcase
; =================
; A tiny command-line demo of explicit MORK stepping.

!(include mork)

!(println! "=== mork MM2 showcase ===")
!(bind! &ws (mork:new-space))
!(mork:add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))
!(println! (before-step-size
             (mork:size &ws)))
!(println! (before-step-edges
             (collapse (mork:match &ws (edge $x $y) ($x $y)))))
!(println! (steps-1
             (mork:step! &ws)))
!(println! (after-step-paths
             (collapse (mork:match &ws (path $x $y) ($x $y)))))
!(println! (steps-2
             (mork:step! &ws)))
!(println! (after-step-size
             (mork:size &ws)))
!(println! "=== showcase complete ===")
