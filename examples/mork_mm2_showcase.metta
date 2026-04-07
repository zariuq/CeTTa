; mork MM2 showcase
; =================
; MM2 rule execution inside a MorkSpace.
;
; MM2 exec rule format:
;   (exec (PRIORITY NAME) PATTERN (O ACTIONS))
;   PATTERN uses (, ...) for conjunction of required atoms.
;   ACTIONS use (+ atom) to add and (- atom) to remove.
;
; This rule: when edges X→Y and Y→Z both exist, derive path X→Z.

!(include mork)

!(println! "=== MM2 stepping ===")
!(bind! &ws (mork:new-space))
!(mork:add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))
!(println! ("before" (collapse (mork:get-atoms &ws))))
!(println! ("step-1" (mork:step! &ws)))
!(println! ("after" (collapse (mork:get-atoms &ws))))
!(println! ("derived" (collapse (mork:match &ws (path $x $y) ($x $y)))))

; Stepping again returns 0 — the space has reached fixpoint.
; No new rule firings are possible with the current atoms.
!(println! ("step-2-fixpoint" (mork:step! &ws)))

!(println! "=== showcase complete ===")
