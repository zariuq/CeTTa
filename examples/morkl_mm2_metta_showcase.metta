; morkl + MM2 + MeTTa showcase
; ============================
; MM2 derives one fact, morkl inspects the exact substrate shape of that
; fact, and ordinary MeTTa annotates the restricted result.

!(include mork)

(= (path-tag a c) strong)
(= (path-why a c) (two-hop a b c))

!(println! "=== integrated morkl + MM2 + MeTTa showcase ===")
!(bind! &ws (mork:new-space))
!(mork:add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))

!(println! (rule-locs
             (collapse (mork:match &ws (exec $loc $pattern $template) $loc))))
!(println! (before-step-edges
             (collapse (mork:match &ws (edge $x $y) ($x $y)))))
!(println! (steps-fired
             (mork:step! &ws 4)))
!(println! (derived-paths
             (collapse (mork:match &ws (path $x $y) ($x $y)))))

!(bind! &path-ac (mork:path-of-atom (path a c)))
!(println! (target-path-bytes &path-ac))

!(bind! &selector (mork:new-space))
!(mork:add-atom &selector (path a c))
!(bind! &derived-only (mork:prefix-restrict &ws &selector))
!(println! (derived-only-size
             (mork:size &derived-only)))

!(bind! &z (mork:zipper &derived-only))
!(mork:zipper-descend-until! &z)
!(println! (zipper-path-bytes
             (mork:zipper-path-bytes &z)))
!(println! (zipper-path-exists
             (mork:zipper-path-exists &z)))
!(println! (zipper-is-val
             (mork:zipper-is-val &z)))
!(mork:zipper-close! &z)

!(bind! &oz (mork:overlay-zipper &derived-only &selector))
!(mork:overlay-zipper-descend-until! &oz)
!(println! (overlay-path-bytes
             (mork:overlay-zipper-path-bytes &oz)))
!(println! (overlay-path-exists
             (mork:overlay-zipper-path-exists &oz)))
!(println! (overlay-is-val
             (mork:overlay-zipper-is-val &oz)))
!(mork:overlay-zipper-close! &oz)

!(println! (metta-analysis
             (collapse
               (mork:match &derived-only (path $x $y)
                 (annotated $x $y (path-tag $x $y) (path-why $x $y))))))
!(println! "=== showcase complete ===")
