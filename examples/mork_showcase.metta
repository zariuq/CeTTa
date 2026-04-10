; mork showcase — cursor and substrate inspection
; ================================================
; This demo exercises the machine-close PathMap/MORK surface:
; creating MorkSpaces, querying them, inspecting encoded byte paths,
; navigating with zippers, and composing views with overlay/product.
;
; If you want algebra (join, meet, subtract) see mork_algebra_showcase.
; If you want MM2 stepping see mork_mm2_showcase or mork_mm2_metta_showcase.

!(include mork)

; ── Basic MorkSpace: create, add, query ─────────────────────
; MorkSpace is a distinct type — use mork: verbs, not generic space verbs.

!(println! "=== MorkSpace basics ===")
!(bind! &m (mork:new-space))
!(mork:add-atom &m (edge a b))
!(mork:add-atom &m (edge a c))
!(println! ("type" (get-type &m)))
!(println! ("atoms" (collapse (mork:get-atoms &m))))
!(println! ("match edge a _"
  (collapse (mork:match &m (edge a $y) $y))))
!(println! ("size" (mork:size &m)))

; ── Encoded path introspection ──────────────────────────────
; Every atom is stored as a byte path in a 256-radix trie.
; mork:path-of-atom shows the raw key; zippers let you walk the trie.

!(println! "=== encoded path of (edge a b) ===")
!(bind! &path-ab (mork:path-of-atom (edge a b)))
!(println! ("path-of" (edge a b) &path-ab))

; ── Zipper: snapshot cursor over the trie ───────────────────
; A zipper is a frozen read cursor. descend-until! walks to the
; first value or branch point. Useful for inspecting trie shape
; without materializing the whole space.

!(println! "=== zipper: walk to first branch/value ===")
!(bind! &mz (mork:zipper &m))
!(println! ("root-children" (mork:zipper-child-count &mz)
            "child-bytes" (mork:zipper-child-bytes &mz)))
!(let $_ (mork:zipper-descend-until! &mz) ())
!(println! ("shared-prefix-bytes" (mork:zipper-path-bytes &mz)))
!(println! ("exists?" (mork:zipper-path-exists &mz)
            "is-val?" (mork:zipper-is-val &mz)))
!(let $_ (mork:zipper-descend-first! &mz) ())
!(println! ("first-leaf-bytes" (mork:zipper-path-bytes &mz)
            "is-val?" (mork:zipper-is-val &mz)))
!(mork:zipper-close! &mz)

; ── Prefix restriction: selector-shaped filtering ───────────
; prefix-restrict keeps only atoms admitted by a valued prefix
; in the selector. Not intersection — the selector acts as a
; prefix gate on the encoded trie, not as a peer operand.

!(println! "=== prefix-restrict: keep only (edge a b) ===")
!(bind! &selector (mork:new-space))
!(mork:add-atom &selector (edge a b))
!(bind! &restricted (mork:clone &m))
!(mork:prefix-restrict &restricted &selector)
!(println! ("restricted" (collapse (mork:get-atoms &restricted))))

; ── Overlay zipper: virtual fused trie (local <|> base) ─────
; Reads the union of two spaces without materializing a join.
; At each path, the overlay returns local's value if present,
; else base's value.

!(println! "=== overlay: restricted + selector ===")
!(bind! &oz (mork:overlay-zipper &restricted &selector))
!(let $_ (mork:overlay-zipper-descend-until! &oz) ())
!(println! ("overlay-focus-bytes" (mork:overlay-zipper-path-bytes &oz)
            "is-val?" (mork:overlay-zipper-is-val &oz)))
!(mork:overlay-zipper-close! &oz)

; ── Product zipper: stitched multi-factor traversal ─────────
; Walks a virtual product of 2-4 spaces without materializing
; the cross product. Each step lands on the factor whose next
; byte is smallest. Useful for coordinated multi-space search.

!(println! "=== product: three-factor traversal ===")
!(bind! &a (mork:new-space))
!(bind! &b (mork:new-space))
!(mork:add-atom &a (x 1))
!(mork:add-atom &b (y 2))
!(bind! &pz (mork:product-zipper &a &b &m))
!(println! ("factors" (mork:product-zipper-factor-count &pz)))
!(let $_ (mork:product-zipper-descend-until! &pz) ())
!(println! ("focus-factor" (mork:product-zipper-focus-factor &pz)
            "product-path-bytes" (mork:product-zipper-path-bytes &pz)))
!(let $_ (mork:product-zipper-descend-until! &pz) ())
!(println! ("focus-factor" (mork:product-zipper-focus-factor &pz)
            "product-path-bytes" (mork:product-zipper-path-bytes &pz)))
!(mork:product-zipper-close! &pz)

!(println! "=== showcase complete ===")
