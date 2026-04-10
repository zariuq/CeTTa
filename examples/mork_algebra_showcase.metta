; mork algebra showcase
; =====================
; Two transit agencies publish overlapping route segments.
; Algebra composes, intersects, and diffs them,
; then MM2 derives all reachable connections from the combined network.

!(include mork)

; ── Agency data ──────────────────────────────────────────────

!(bind! &north (mork:new-space))
!(mork:add-atoms &north
  ((route alpha bravo)
   (route bravo charlie)
   (route charlie delta)))

!(bind! &south (mork:new-space))
!(mork:add-atoms &south
  ((route bravo charlie)
   (route charlie delta)
   (route delta echo)
   (route echo foxtrot)))

!(println! "=== two transit agencies ===")
!(println! ("north" (collapse (mork:get-atoms &north))))
!(println! ("south" (collapse (mork:get-atoms &south))))

; ── Meet: backbone segments both agencies serve ─────────────

!(bind! &backbone (mork:clone &north))
!(mork:meet &backbone &south)
!(println! "=== meet: shared backbone ===")
!(println! ("backbone" (collapse (mork:get-atoms &backbone))))

; ── Subtract: segments unique to each agency ────────────────

!(bind! &north-only (mork:clone &north))
!(mork:subtract &north-only &south)
!(bind! &south-only (mork:clone &south))
!(mork:subtract &south-only &north)
!(println! "=== subtract: unique segments ===")
!(println! ("north-only" (collapse (mork:get-atoms &north-only))))
!(println! ("south-only" (collapse (mork:get-atoms &south-only))))

; ── Join: merge networks, add transit rule ──────────────────

!(bind! &network (mork:clone &north))
!(mork:join &network &south)
!(mork:add-atom &network
  (exec (0 hop)
    (, (route $x $y)
       (route $y $z))
    (O (+ (conn $x $z)))))
!(println! "=== join: merged network before stepping ===")
!(println! ("all-routes"
  (collapse (mork:match &network (route $x $y) ($x $y)))))

; ── Branch BEFORE stepping: what if delta–echo closes? ──────

!(bind! &closure (mork:new-space))
!(mork:add-atom &closure (route delta echo))
!(bind! &disrupted (mork:clone &network))
!(mork:subtract &disrupted &closure)

; Step both independently
!(println! ("network-steps" (mork:step! &network 4)))
!(println! ("disrupted-steps" (mork:step! &disrupted 4)))

!(println! "=== full network reachability ===")
!(println! ("conn"
  (collapse (mork:match &network (conn $x $y) ($x $y)))))
!(println! "=== disrupted: delta-echo removed before derivation ===")
!(println! ("conn"
  (collapse (mork:match &disrupted (conn $x $y) ($x $y)))))
!(println! ("lost"
  delta echo "closed => no (conn delta foxtrot) or (conn charlie echo)"))

!(println! "=== showcase complete ===")
