; CeTTa MORK-facing helper surface.
; Intentionally explicit and honest: MorkSpace is its own public value kind,
; not an ordinary generic MeTTa SpaceType. Use the mork: namespace for
; explicit MORK construction, querying, mutation, algebra, and cursor work.
; Generic core space constructors and backend mutation no longer expose the
; MORK lane.

(: MorkSpace Type)

(: mork:new-space (-> MorkSpace))
(: mork:new-space (-> Atom MorkSpace))
(: mork:add-atom (-> MorkSpace Atom (->)))
(: mork:add-atoms (-> MorkSpace Expression (->)))
(: mork:remove-atom (-> MorkSpace Atom (->)))
(: mork:get-atoms (-> MorkSpace Atom))
(: mork:size (-> MorkSpace Number))
(: mork:match (-> MorkSpace Atom Atom Atom))
(: mork:clone (-> MorkSpace MorkSpace))
(: mork:step! (-> MorkSpace Number))
(: mork:step! (-> MorkSpace Number Number))
(: mork:include! (-> MorkSpace Atom (->)))

(: mork:open-act (-> Atom MorkSpace))
(: mork:open-act (-> Atom Atom MorkSpace))
(: mork:dump! (-> MorkSpace Atom (->)))
(: mork:load-act! (-> MorkSpace Atom (->)))
(: mork:join (-> MorkSpace MorkSpace (->)))
(: mork:meet (-> MorkSpace MorkSpace (->)))
(: mork:subtract (-> MorkSpace MorkSpace (->)))
(: mork:prefix-restrict (-> MorkSpace MorkSpace (->)))
(: mork:path-of-atom (-> Atom Atom))
(: mork:zipper (-> MorkSpace Atom))
(: mork:zipper-close! (-> Atom (->)))
(: mork:zipper-path-exists (-> Atom Bool))
(: mork:zipper-is-val (-> Atom Bool))
(: mork:zipper-child-count (-> Atom Number))
(: mork:zipper-path-bytes (-> Atom Atom))
(: mork:zipper-child-bytes (-> Atom Atom))
(: mork:zipper-val-count (-> Atom Number))
(: mork:zipper-depth (-> Atom Number))
(: mork:zipper-reset! (-> Atom (->)))
(: mork:zipper-ascend! (-> Atom Number Bool))
(: mork:zipper-descend-byte! (-> Atom Number Bool))
(: mork:zipper-descend-index! (-> Atom Number Bool))
(: mork:zipper-descend-first! (-> Atom Bool))
(: mork:zipper-descend-last! (-> Atom Bool))
(: mork:zipper-descend-until! (-> Atom Bool))
(: mork:zipper-descend-until-max-bytes! (-> Atom Number Bool))
(: mork:zipper-ascend-until! (-> Atom Bool))
(: mork:zipper-ascend-until-branch! (-> Atom Bool))
(: mork:zipper-next-sibling-byte! (-> Atom Bool))
(: mork:zipper-prev-sibling-byte! (-> Atom Bool))
(: mork:zipper-next-step! (-> Atom Bool))
(: mork:zipper-next-val! (-> Atom Bool))
(: mork:zipper-fork (-> Atom Atom))
(: mork:zipper-make-map (-> Atom MorkSpace))
(: mork:zipper-make-snapshot-map (-> Atom MorkSpace))
(: mork:product-zipper (-> MorkSpace MorkSpace Atom))
(: mork:product-zipper (-> MorkSpace MorkSpace MorkSpace Atom))
(: mork:product-zipper (-> MorkSpace MorkSpace MorkSpace MorkSpace Atom))
(: mork:product-zipper-close! (-> Atom (->)))
(: mork:product-zipper-path-exists (-> Atom Bool))
(: mork:product-zipper-is-val (-> Atom Bool))
(: mork:product-zipper-child-count (-> Atom Number))
(: mork:product-zipper-path-bytes (-> Atom Atom))
(: mork:product-zipper-child-bytes (-> Atom Atom))
(: mork:product-zipper-val-count (-> Atom Number))
(: mork:product-zipper-depth (-> Atom Number))
(: mork:product-zipper-factor-count (-> Atom Number))
(: mork:product-zipper-focus-factor (-> Atom Number))
(: mork:product-zipper-path-indices (-> Atom Atom))
(: mork:product-zipper-reset! (-> Atom (->)))
(: mork:product-zipper-ascend! (-> Atom Number Bool))
(: mork:product-zipper-descend-byte! (-> Atom Number Bool))
(: mork:product-zipper-descend-index! (-> Atom Number Bool))
(: mork:product-zipper-descend-first! (-> Atom Bool))
(: mork:product-zipper-descend-last! (-> Atom Bool))
(: mork:product-zipper-descend-until! (-> Atom Bool))
(: mork:product-zipper-descend-until-max-bytes! (-> Atom Number Bool))
(: mork:product-zipper-ascend-until! (-> Atom Bool))
(: mork:product-zipper-ascend-until-branch! (-> Atom Bool))
(: mork:product-zipper-next-sibling-byte! (-> Atom Bool))
(: mork:product-zipper-prev-sibling-byte! (-> Atom Bool))
(: mork:product-zipper-next-step! (-> Atom Bool))
(: mork:product-zipper-next-val! (-> Atom Bool))
(: mork:overlay-zipper (-> MorkSpace MorkSpace Atom))
(: mork:overlay-zipper-close! (-> Atom (->)))
(: mork:overlay-zipper-path-exists (-> Atom Bool))
(: mork:overlay-zipper-is-val (-> Atom Bool))
(: mork:overlay-zipper-child-count (-> Atom Number))
(: mork:overlay-zipper-path-bytes (-> Atom Atom))
(: mork:overlay-zipper-child-bytes (-> Atom Atom))
(: mork:overlay-zipper-depth (-> Atom Number))
(: mork:overlay-zipper-reset! (-> Atom (->)))
(: mork:overlay-zipper-ascend! (-> Atom Number Bool))
(: mork:overlay-zipper-descend-byte! (-> Atom Number Bool))
(: mork:overlay-zipper-descend-index! (-> Atom Number Bool))
(: mork:overlay-zipper-descend-first! (-> Atom Bool))
(: mork:overlay-zipper-descend-last! (-> Atom Bool))
(: mork:overlay-zipper-descend-until! (-> Atom Bool))
(: mork:overlay-zipper-descend-until-max-bytes! (-> Atom Number Bool))
(: mork:overlay-zipper-ascend-until! (-> Atom Bool))
(: mork:overlay-zipper-ascend-until-branch! (-> Atom Bool))
(: mork:overlay-zipper-next-sibling-byte! (-> Atom Bool))
(: mork:overlay-zipper-prev-sibling-byte! (-> Atom Bool))
(: mork:overlay-zipper-next-step! (-> Atom Bool))

(@doc mork:new-space
  (@desc "Creates an explicit MorkSpace handle. This stays separate from ordinary SpaceType values even though the implementation reuses the PathMap substrate underneath. The zero-argument form defaults to atom discipline; the one-argument form accepts an unordered discipline such as atom or hash. This is the blessed public constructor for the MORK lane.")
  (@params (
    (@param "Optional unordered discipline such as atom or hash")))
  (@return "Fresh MorkSpace handle"))

(@doc mork:add-atom
  (@desc "Adds one atom to an explicit MorkSpace through the namespaced MORK surface.")
  (@params (
    (@param "Explicit MorkSpace handle")
    (@param "Atom to add")))
  (@return "Unit on success"))

(@doc mork:match
  (@desc "Explicit MORK query surface over MorkSpace.")
  (@params (
    (@param "Explicit MorkSpace handle")
    (@param "Query pattern")
    (@param "Template/result expression")))
  (@return "Superposed query results"))

(@doc mork:clone
  (@desc "Creates a fresh MorkSpace snapshot while preserving the source space's encoding universe. Use this when you want persistence before applying mutating algebra.")
  (@params (
    (@param "MorkSpace handle to snapshot")))
  (@return "Fresh cloned MorkSpace"))

(@doc mork:get-atoms
  (@desc "Explicit enumeration surface for MorkSpace. Returns the current atom fan-out without pretending the value is an ordinary generic CeTTa space.")
  (@params (
    (@param "Explicit MorkSpace handle")))
  (@return "Superposed atoms currently visible in the space"))

(@doc mork:size
  (@desc "Explicit size/count surface for MorkSpace. Reports the current unique structural support size, not duplicate-aware ACT logical copy counts.")
  (@params (
    (@param "Explicit MorkSpace handle")))
  (@return "Current unique structural support count"))

(@doc mork:step!
  (@desc "Explicit MORK execution surface. Advances an unordered MorkSpace and returns the number of steps actually performed. Attached compiled ACT spaces materialize into live storage before stepping.")
  (@params (
    (@param "MorkSpace to advance")
    (@param "Optional non-negative step limit")))
  (@return "Number of executed steps"))

(@doc mork:include!
  (@desc "Loads a module into an existing MorkSpace. This is the explicit native MORK lane for direct .mm2 and compiled .act modules; generic include/import reject raw .mm2 specs and leave them to mork:include! instead of lowering them through ordinary CeTTa space materialization.")
  (@params (
    (@param "Destination MorkSpace handle")
    (@param "Module spec or path to include")))
  (@return "Unit on success"))

(@doc mork:dump!
  (@desc "Canonical MORK umbrella helper for ACT export. Compiles the current unordered MorkSpace into a PathMap .act artifact at the given path. When mirrored row metadata is available, duplicate indexed copies are preserved through the ACT sidecar metadata; otherwise the export follows structural support.")
  (@params (
    (@param "MorkSpace to compile into a .act artifact")
    (@param "Output ACT filename")))
  (@return "Unit on success"))

(@doc mork:load-act!
  (@desc "Canonical MORK umbrella helper for additive ACT materialization. Loads atoms from a compiled .act artifact into an existing MorkSpace through CeTTa's bridge load path.")
  (@params (
    (@param "Target MorkSpace that will receive the materialized atoms")
    (@param "ACT filename to load from")))
  (@return "Unit on success"))

(@doc mork:open-act
  (@desc "Canonical MORK umbrella helper for attached compiled querying. Opens a compiled .act artifact as a MorkSpace handle backed by attached compiled storage. The one-argument form defaults to hash discipline; the two-argument form accepts an explicit unordered discipline such as atom or hash.")
  (@params (
    (@param "Optional unordered discipline")
    (@param "ACT filename to open")))
  (@return "Fresh MorkSpace handle attached to compiled ACT data"))

(@doc mork:join
  (@desc "PathMap structural union. Mutates the left destination space in place. Use mork:clone first when you want a persistent branch.")
  (@params (
    (@param "Destination MorkSpace to mutate")
    (@param "Right MorkSpace")))
  (@return "Unit on success"))

(@doc mork:meet
  (@desc "PathMap structural intersection. Mutates the left destination space in place.")
  (@params (
    (@param "Destination MorkSpace to mutate")
    (@param "Right MorkSpace")))
  (@return "Unit on success"))

(@doc mork:subtract
  (@desc "PathMap structural subtraction. Mutates the left destination space in place.")
  (@params (
    (@param "Destination MorkSpace to mutate")
    (@param "Right MorkSpace")))
  (@return "Unit on success"))

(@doc mork:prefix-restrict
  (@desc "Selector-shaped restriction. The right-hand operand acts as a valued-prefix selector over encoded paths, not as a logical query pattern. Mutates the left destination space in place.")
  (@params (
    (@param "Destination MorkSpace whose support is being filtered")
    (@param "Right MorkSpace acting as a valued-prefix selector")))
  (@return "Unit on success"))

(@doc mork:path-of-atom
  (@desc "Encodes one ordinary CeTTa atom through the live MORK/PathMap parser path and returns the exact byte path currently used as the structural key in the trie. This is an expert projection, not a logical query operation.")
  (@params (
    (@param "Atom to encode through the bridge parser")))
  (@return "Expression of byte numbers in 0..255"))

(@doc mork:zipper
  (@desc "Creates a direct read-only zipper snapshot over a MorkSpace. This is the first machine-close PathMap cursor surface: movement is explicit, values are inspected by local shape, and structural maps or rooted focus snapshots are materialized only when asked. The returned handle is an opaque typed native resource: wrong cursor-kind calls and use-after-close fail as ordinary errors instead of being coerced across families.")
  (@params (
    (@param "MorkSpace handle")))
  (@return "Native mork-cursor handle"))

(@doc mork:zipper-path-bytes
  (@desc "Returns the exact byte path from the zipper's root to its current focus. This is a direct path-level view of the cursor, intended for expert inspection and later selector/prefix work.")
  (@params (
    (@param "mork-cursor handle")))
  (@return "Expression of byte numbers in 0..255"))

(@doc mork:zipper-child-bytes
  (@desc "Returns the ascending set-bit projection of the current child mask: one byte number for each immediate child branch reachable from the focus. This is an exact view of which continuation bytes exist, without pretending they are ordinary atoms.")
  (@params (
    (@param "mork-cursor handle")))
  (@return "Expression of child byte numbers in ascending order"))

(@doc mork:zipper-fork
  (@desc "Duplicates the current zipper state at the same snapshot and path. This is the cursor analogue of PathMap fork: it does not re-root at the current focus.")
  (@params (
    (@param "mork-cursor handle")))
  (@return "Fresh mork-cursor handle at the same focus"))

(@doc mork:zipper-make-map
  (@desc "Materializes the current zipper focus as a fresh structural MorkSpace using PathMap's raw make-map semantics. This is the direct structural export primitive; it does not add any extra CeTTa-side transformation beyond the substrate behavior, and can therefore agree with the snapshot form when the focused structural subtrie already carries a root value.")
  (@params (
    (@param "mork-cursor handle")))
  (@return "Fresh structural MorkSpace rooted below the current focus"))

(@doc mork:zipper-make-snapshot-map
  (@desc "Materializes the current zipper focus as a fresh rooted snapshot MorkSpace. This is the PathMap snapshot export form built on top of make-map semantics, so any extra grafting at [] is exactly the substrate's snapshot behavior rather than a CeTTa-specific rewrite.")
  (@params (
    (@param "mork-cursor handle")))
  (@return "Fresh rooted snapshot MorkSpace"))

(@doc mork:product-zipper
  (@desc "Creates a direct read-only product zipper over 2 to 4 MorkSpaces. Exposes the multi-factor traversal that PathMap uses for conjunction-style search, without materializing a brute-force join. The returned handle is opaque and product-specific.")
  (@params (
    (@param "Two to four MorkSpace handles")))
  (@return "Native mork-product-cursor handle"))

(@doc mork:product-zipper-path-indices
  (@desc "Returns the factor-boundary indices of the current product path as a sequence of non-negative integers. This is a direct expert view of how the combined product path is segmented across factors.")
  (@params (
    (@param "mork-product-cursor handle")))
  (@return "Expression of factor-boundary indices"))

(@doc mork:overlay-zipper
  (@desc "Creates a direct read-only overlay zipper over two MorkSpaces. Exposes the virtual fused-trie view (local <|> base) without materializing a join. The returned handle is opaque and overlay-specific. Sibling and step traversal are exposed; lazy next-val iteration is not, as the underlying overlay zipper does not provide it yet.")
  (@params (
    (@param "Base MorkSpace")
    (@param "Overlay MorkSpace")))
  (@return "Native mork-overlay-cursor handle"))

(= (mork:new-space)
  (function
    (case (__cetta_lib_mork_space_new ())
      (($result (return $result))))))

(= (mork:new-space $kind)
  (function
    (case (__cetta_lib_mork_space_new $kind)
      (($result (return $result))))))

(= (mork:add-atom $space $atom)
  (function
    (case (__cetta_lib_mork_space_add_atom $space (quote $atom))
      (($result (return $result))))))

(= (mork:add-atoms $space $tuple)
  (function
    (case (__cetta_lib_mork_space_add_atoms $space $tuple)
      (($result (return $result))))))

(= (mork:remove-atom $space $atom)
  (function
    (case (__cetta_lib_mork_space_remove_atom $space (quote $atom))
      (($result (return $result))))))
(= (mork:size $space)
  (function
    (case (__cetta_lib_mork_space_size $space)
      (($result (return $result))))))

(= (mork:clone $space)
  (function
    (case (__cetta_lib_mork_clone $space)
      (($result (return $result))))))

(= (mork:step! $space)
  (mork:step! $space 1))

(= (mork:step! $space $steps)
  (function
    (case (__cetta_lib_mork_space_step $space $steps)
      (($result (return $result))))))

(= (mork:include! $space $spec)
  (function
    (case (__cetta_lib_mork_space_include $space $spec)
      (($result (return $result))))))

(= (mork:open-act $path)
  (function
    (case (__cetta_lib_mork_space_open_act $path)
      (($result (return $result))))))

(= (mork:open-act $kind $path)
  (function
    (case (__cetta_lib_mork_space_open_act $kind $path)
      (($result (return $result))))))

(= (mork:dump! $space $path)
  (function
    (case (__cetta_lib_mork_space_dump_act $space $path)
      (($result (return $result))))))

(= (mork:load-act! $space $path)
  (function
    (case (__cetta_lib_mork_space_import_act $space $path)
      (($result (return $result))))))

(= (mork:join $lhs $rhs)
  (function
    (case (__cetta_lib_mork_join $lhs $rhs)
      (($result (return $result))))))

(= (mork:meet $lhs $rhs)
  (function
    (case (__cetta_lib_mork_meet $lhs $rhs)
      (($result (return $result))))))

(= (mork:subtract $lhs $rhs)
  (function
    (case (__cetta_lib_mork_subtract $lhs $rhs)
      (($result (return $result))))))

(= (mork:prefix-restrict $lhs $rhs)
  (function
    (case (__cetta_lib_mork_restrict $lhs $rhs)
      (($result (return $result))))))

(= (mork:path-of-atom $atom)
  (function
    (case (__cetta_lib_mork_path_of_atom $atom)
      (($result (return $result))))))

(= (mork:zipper $space)
  (function
    (case (__cetta_lib_mork_zipper_new $space)
      (($result (return $result))))))

(= (mork:zipper-close! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_close $zipper)
      (($result (return $result))))))

(= (mork:zipper-path-exists $zipper)
  (function
    (case (__cetta_lib_mork_zipper_path_exists $zipper)
      (($result (return $result))))))

(= (mork:zipper-is-val $zipper)
  (function
    (case (__cetta_lib_mork_zipper_is_val $zipper)
      (($result (return $result))))))

(= (mork:zipper-child-count $zipper)
  (function
    (case (__cetta_lib_mork_zipper_child_count $zipper)
      (($result (return $result))))))

(= (mork:zipper-path-bytes $zipper)
  (function
    (case (__cetta_lib_mork_zipper_path_bytes $zipper)
      (($result (return $result))))))

(= (mork:zipper-child-bytes $zipper)
  (function
    (case (__cetta_lib_mork_zipper_child_bytes $zipper)
      (($result (return $result))))))

(= (mork:zipper-val-count $zipper)
  (function
    (case (__cetta_lib_mork_zipper_val_count $zipper)
      (($result (return $result))))))

(= (mork:zipper-depth $zipper)
  (function
    (case (__cetta_lib_mork_zipper_depth $zipper)
      (($result (return $result))))))

(= (mork:zipper-reset! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_reset $zipper)
      (($result (return $result))))))

(= (mork:zipper-ascend! $zipper $steps)
  (function
    (case (__cetta_lib_mork_zipper_ascend $zipper $steps)
      (($result (return $result))))))

(= (mork:zipper-descend-byte! $zipper $byte)
  (function
    (case (__cetta_lib_mork_zipper_descend_byte $zipper $byte)
      (($result (return $result))))))

(= (mork:zipper-descend-index! $zipper $index)
  (function
    (case (__cetta_lib_mork_zipper_descend_index $zipper $index)
      (($result (return $result))))))

(= (mork:zipper-descend-first! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_descend_first $zipper)
      (($result (return $result))))))

(= (mork:zipper-descend-last! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_descend_last $zipper)
      (($result (return $result))))))

(= (mork:zipper-descend-until! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_descend_until $zipper)
      (($result (return $result))))))

(= (mork:zipper-descend-until-max-bytes! $zipper $max-bytes)
  (function
    (case (__cetta_lib_mork_zipper_descend_until_max_bytes $zipper $max-bytes)
      (($result (return $result))))))

(= (mork:zipper-ascend-until! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_ascend_until $zipper)
      (($result (return $result))))))

(= (mork:zipper-ascend-until-branch! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_ascend_until_branch $zipper)
      (($result (return $result))))))

(= (mork:zipper-next-sibling-byte! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_next_sibling_byte $zipper)
      (($result (return $result))))))

(= (mork:zipper-prev-sibling-byte! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_prev_sibling_byte $zipper)
      (($result (return $result))))))

(= (mork:zipper-next-step! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_next_step $zipper)
      (($result (return $result))))))

(= (mork:zipper-next-val! $zipper)
  (function
    (case (__cetta_lib_mork_zipper_next_val $zipper)
      (($result (return $result))))))

(= (mork:zipper-fork $zipper)
  (function
    (case (__cetta_lib_mork_zipper_fork $zipper)
      (($result (return $result))))))

(= (mork:zipper-make-map $zipper)
  (function
    (case (__cetta_lib_mork_zipper_make_map $zipper)
      (($result (return $result))))))

(= (mork:zipper-make-snapshot-map $zipper)
  (function
    (case (__cetta_lib_mork_zipper_make_snapshot_map $zipper)
      (($result (return $result))))))

(= (mork:product-zipper $lhs $rhs)
  (function
    (case (__cetta_lib_mork_product_zipper_new $lhs $rhs)
      (($result (return $result))))))

(= (mork:product-zipper $a $b $c)
  (function
    (case (__cetta_lib_mork_product_zipper_new $a $b $c)
      (($result (return $result))))))

(= (mork:product-zipper $a $b $c $d)
  (function
    (case (__cetta_lib_mork_product_zipper_new $a $b $c $d)
      (($result (return $result))))))

(= (mork:product-zipper-close! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_close $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-path-exists $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_path_exists $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-is-val $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_is_val $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-child-count $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_child_count $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-path-bytes $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_path_bytes $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-child-bytes $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_child_bytes $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-val-count $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_val_count $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-depth $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_depth $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-factor-count $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_factor_count $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-focus-factor $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_focus_factor $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-path-indices $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_path_indices $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-reset! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_reset $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-ascend! $zipper $steps)
  (function
    (case (__cetta_lib_mork_product_zipper_ascend $zipper $steps)
      (($result (return $result))))))

(= (mork:product-zipper-descend-byte! $zipper $byte)
  (function
    (case (__cetta_lib_mork_product_zipper_descend_byte $zipper $byte)
      (($result (return $result))))))

(= (mork:product-zipper-descend-index! $zipper $index)
  (function
    (case (__cetta_lib_mork_product_zipper_descend_index $zipper $index)
      (($result (return $result))))))

(= (mork:product-zipper-descend-first! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_descend_first $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-descend-last! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_descend_last $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-descend-until! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_descend_until $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-descend-until-max-bytes! $zipper $max-bytes)
  (function
    (case (__cetta_lib_mork_product_zipper_descend_until_max_bytes $zipper $max-bytes)
      (($result (return $result))))))

(= (mork:product-zipper-ascend-until! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_ascend_until $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-ascend-until-branch! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_ascend_until_branch $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-next-sibling-byte! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_next_sibling_byte $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-prev-sibling-byte! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_prev_sibling_byte $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-next-step! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_next_step $zipper)
      (($result (return $result))))))

(= (mork:product-zipper-next-val! $zipper)
  (function
    (case (__cetta_lib_mork_product_zipper_next_val $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper $base $overlay)
  (function
    (case (__cetta_lib_mork_overlay_zipper_new $base $overlay)
      (($result (return $result))))))

(= (mork:overlay-zipper-close! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_close $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-path-exists $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_path_exists $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-is-val $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_is_val $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-child-count $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_child_count $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-path-bytes $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_path_bytes $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-child-bytes $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_child_bytes $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-depth $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_depth $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-reset! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_reset $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-ascend! $zipper $steps)
  (function
    (case (__cetta_lib_mork_overlay_zipper_ascend $zipper $steps)
      (($result (return $result))))))

(= (mork:overlay-zipper-descend-byte! $zipper $byte)
  (function
    (case (__cetta_lib_mork_overlay_zipper_descend_byte $zipper $byte)
      (($result (return $result))))))

(= (mork:overlay-zipper-descend-index! $zipper $index)
  (function
    (case (__cetta_lib_mork_overlay_zipper_descend_index $zipper $index)
      (($result (return $result))))))

(= (mork:overlay-zipper-descend-first! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_descend_first $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-descend-last! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_descend_last $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-descend-until! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_descend_until $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-descend-until-max-bytes! $zipper $max-bytes)
  (function
    (case (__cetta_lib_mork_overlay_zipper_descend_until_max_bytes $zipper $max-bytes)
      (($result (return $result))))))

(= (mork:overlay-zipper-ascend-until! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_ascend_until $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-ascend-until-branch! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_ascend_until_branch $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-next-sibling-byte! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_next_sibling_byte $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-prev-sibling-byte! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_prev_sibling_byte $zipper)
      (($result (return $result))))))

(= (mork:overlay-zipper-next-step! $zipper)
  (function
    (case (__cetta_lib_mork_overlay_zipper_next_step $zipper)
      (($result (return $result))))))
