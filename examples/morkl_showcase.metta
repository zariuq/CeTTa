; morkl showcase
; ==============
; A small command-line demo of the explicit MORK lane and the
; machine-close PathMap/MORK cursor surface exposed via mork:.

!(include mork)

!(println! "=== morkl showcase: explicit MorkSpace lane ===")
!(bind! &m (mork:new-space))
!(println! (space-type (get-type &m)))
!(mork:add-atom &m (edge a b))
!(mork:add-atom &m (edge a c))
!(println! (mork-match
             (collapse (mork:match &m (edge $x $y) ($x $y)))))
!(println! (mork-size (mork:size &m)))

!(println! "=== encoded path introspection ===")
!(bind! &path-ab (mork:path-of-atom (edge a b)))
!(println! (path-of-edge-a-b &path-ab))
!(bind! &mz (mork:zipper &m))
!(println! (root-child-bytes
             (mork:zipper-child-bytes &mz)))
!(bind! &zipper-stepped (mork:zipper-descend-until! &mz))
!(println! (zipper-path-bytes
             (mork:zipper-path-bytes &mz)))
!(println! (zipper-path-exists
             (mork:zipper-path-exists &mz)))
!(println! (zipper-is-val
             (mork:zipper-is-val &mz)))
!(mork:zipper-close! &mz)

!(println! "=== selector-shaped prefix restriction ===")
!(bind! &selector (mork:new-space))
!(mork:add-atom &selector (edge a b))
!(bind! &restricted (mork:prefix-restrict &m &selector))
!(println! (restricted-size
             (mork:size &restricted)))
!(println! (restricted-match
             (collapse (mork:match &restricted (edge $x $y) ($x $y)))))

!(println! "=== overlay zipper over restricted plus selector ===")
!(bind! &oz (mork:overlay-zipper &restricted &selector))
!(bind! &overlay-stepped (mork:overlay-zipper-descend-until! &oz))
!(println! (overlay-path-bytes
             (mork:overlay-zipper-path-bytes &oz)))
!(println! (overlay-path-exists
             (mork:overlay-zipper-path-exists &oz)))
!(println! (overlay-is-val
             (mork:overlay-zipper-is-val &oz)))
!(mork:overlay-zipper-close! &oz)

!(println! "=== stitched multi-factor product traversal ===")
!(bind! &a (mork:new-space))
!(bind! &b (mork:new-space))
!(mork:add-atom &a a)
!(mork:add-atom &b b)
!(bind! &pz (mork:product-zipper &a &b &m))
!(println! (product-factor-count
             (mork:product-zipper-factor-count &pz)))
!(bind! &product-step-1 (mork:product-zipper-descend-until! &pz))
!(println! (product-focus-factor
             (mork:product-zipper-focus-factor &pz)))
!(println! (product-path-bytes
             (mork:product-zipper-path-bytes &pz)))
!(println! (product-path-indices
             (mork:product-zipper-path-indices &pz)))
!(bind! &product-step-2 (mork:product-zipper-descend-until! &pz))
!(println! (product-focus-factor-2
             (mork:product-zipper-focus-factor &pz)))
!(println! (product-path-bytes-2
             (mork:product-zipper-path-bytes &pz)))
!(println! (product-path-indices-2
             (mork:product-zipper-path-indices &pz)))
!(mork:product-zipper-close! &pz)

!(println! "=== showcase complete ===")
