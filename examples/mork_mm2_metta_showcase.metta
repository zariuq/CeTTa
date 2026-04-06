; mork + MM2 + MeTTa showcase
; ===========================
; MM2 derives a fact, mork inspects its exact substrate encoding,
; and ordinary MeTTa annotates the result with semantic metadata.
;
; MM2 exec rule format:
;   (exec (PRIORITY NAME) PATTERN (O ACTIONS))
;   See mork_mm2_showcase.metta for details.

!(include mork)

; MeTTa equations for semantic annotation of derived facts.
(= (path-tag a c) strong)
(= (path-why a c) (two-hop a b c))

!(println! "=== derive a path via MM2 ===")
!(bind! &ws (mork:new-space))
!(mork:add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))
!(println! ("before" (collapse (mork:get-atoms &ws))))
!(println! ("steps" (mork:step! &ws 4)))
!(println! ("derived" (collapse (mork:match &ws (path $x $y) ($x $y)))))

; ── Inspect the encoding of the derived atom ────────────────
; The byte path is the raw trie key for (path a c).
!(println! "=== substrate encoding ===")
!(println! ("path-a-c-bytes" (mork:path-of-atom (path a c))))

; ── Prefix-restrict to isolate the derived fact ─────────────
!(bind! &selector (mork:new-space))
!(mork:add-atom &selector (path a c))
!(bind! &derived-only (mork:clone &ws))
!(mork:prefix-restrict &derived-only &selector)
!(println! "=== prefix-restrict: only derived paths ===")
!(println! ("filtered" (collapse (mork:get-atoms &derived-only))))

; ── Zipper: frozen cursor on the filtered workspace ─────────
; descend-until! walks to the first value or branch.
!(println! "=== zipper on filtered workspace ===")
!(bind! &z (mork:zipper &derived-only))
!(let $_ (mork:zipper-descend-until! &z) ())
!(println! ("at" (mork:zipper-path-bytes &z)
            "is-val?" (mork:zipper-is-val &z)))
!(mork:zipper-close! &z)

; ── Overlay: virtual union of filtered + selector ───────────
; Reads local <|> base at each path without materializing a join.
!(println! "=== overlay: filtered + selector ===")
!(bind! &oz (mork:overlay-zipper &derived-only &selector))
!(let $_ (mork:overlay-zipper-descend-until! &oz) ())
!(println! ("at" (mork:overlay-zipper-path-bytes &oz)
            "is-val?" (mork:overlay-zipper-is-val &oz)))
!(mork:overlay-zipper-close! &oz)

; ── MeTTa annotation: semantic analysis of derived facts ────
; Ordinary MeTTa equations attach meaning to what MORK derived.
!(println! "=== MeTTa analysis ===")
!(println! ("annotated"
  (collapse
    (mork:match &derived-only (path $x $y)
      (annotated $x $y (path-tag $x $y) (path-why $x $y))))))

!(println! "=== showcase complete ===")
