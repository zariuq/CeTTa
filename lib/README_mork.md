# `lib/mork.metta` How-To

This note is the practical guide to the explicit `mork:` lane in CeTTa.

The short version:

- load `lib/mork.metta`
- create a `MorkSpace` with `mork:new-space`
- use `mork:add-atom`, `mork:match`, `mork:get-atoms`, `mork:size`, `mork:clone`, and `mork:step!`
- use `mork:` algebra as destination-mutating workspace operations, and use cursor operators when you want machine-close PathMap/MORK behavior
- do **not** treat `MorkSpace` as an ordinary generic `Space`

## 1. Load The Library

For a script or demo:

```metta
!(include mork)
```

For a file that wants to define helper equations in the current space:

```metta
!(import! &self mork)
```

Positive example:

```metta
!(include mork)
!(bind! &ws (mork:new-space))
```

Negative example:

```metta
!(bind! &ws (new-space mork))
```

That generic spelling is intentionally disabled.

## 2. `MorkSpace` Is Not A Generic Space

`mork:new-space` returns a `MorkSpace`, not `(Space mork)` and not a generic `SpaceType`.

```metta
!(include mork)
!(bind! &ws (mork:new-space))
!(println! (space-type (get-type &ws)))
```

Expected shape:

```metta
(space-type MorkSpace)
```

Positive example:

```metta
!(mork:add-atom &ws (edge a b))
!(mork:match &ws (edge $x $y) ($x $y))
```

Negative example:

```metta
!(add-atom &ws (edge a b))
!(match &ws (edge $x $y) ($x $y))
```

Bare generic space verbs reject `MorkSpace` on purpose.

## 3. Basic Mutation And Query

### Create a workspace

```metta
!(bind! &ws (mork:new-space))
```

You can also request an unordered discipline explicitly:

```metta
!(bind! &ws (mork:new-space hash))
```

### Add atoms

```metta
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))
```

Bulk add:

```metta
!(mork:add-atoms &ws ((edge a b) (edge b c)))
```

Remove:

```metta
!(mork:remove-atom &ws (edge a b))
```

### Query

`mork:match` is nondeterministic in normal MeTTa style. Use `collapse` when you want a tuple for printing or later host-side processing.

```metta
!(println! (collapse (mork:match &ws (edge $x $y) ($x $y))))
```

Get all visible atoms:

```metta
!(println! (collapse (mork:get-atoms &ws)))
```

Count (unique structural support, not duplicate-aware ACT logical copies):

```metta
!(println! (mork:size &ws))
```

Clone (creates a fresh snapshot sharing trie structure with the source):

```metta
!(bind! &copy (mork:clone &ws))
```

Mutations on the clone do not affect the original, and vice versa.

Positive example:

```metta
!(collapse (mork:match &ws (edge $x $y) ($x $y)))
```

Negative example:

```metta
!(mork:match &ws (edge missing $y) ($y))
```

An empty `mork:match` result is still one of the rougher current wrapper seams. For polished “absence” checks, prefer exported ordinary-space inspection or a positively phrased query.

## 4. MM2 Stepping

`MorkSpace` is the live MM2 workspace lane.

```metta
!(include mork)
!(bind! &ws (mork:new-space))
!(mork:add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))
!(println! (mork:step! &ws 4))
!(println! (collapse (mork:match &ws (path $x $y) ($x $y))))
```

Positive example:

- `mork:step!` advances the live workspace.

Negative example:

- `mork:join`, `mork:prefix-restrict`, zipper inspection, and the other expert operators are **not** stepping operations.

## 5. ACT Dump, Load, And Open

Dump a live workspace to ACT:

```metta
!(mork:dump! &ws "../runtime/example.act")
```

When mirrored row metadata is available, ACT export preserves duplicate indexed copies through the sidecar metadata; otherwise export follows structural support.

Load ACT contents into an existing live workspace:

```metta
!(bind! &dst (mork:new-space))
!(mork:load-act! &dst "../runtime/example.act")
```

Open an attached compiled ACT directly as a `MorkSpace`:

```metta
!(bind! &compiled (mork:open-act "../runtime/example.act"))
```

Positive example:

- `mork:open-act` returns a `MorkSpace` handle attached to compiled storage.

Negative example:

- opening ACT is not the same as generic `import!` into an ordinary space.

## 6. Workspace Algebra

These operators mutate the left-hand (destination) workspace in place using PathMap's native WriteZipper algebra. The right-hand workspace is read but not modified.

If you want persistence before mutating, clone first:

```metta
!(bind! &branch (mork:clone &ws))
```

### Join (union)

Merge all paths from `&rhs` into `&lhs`. Paths already in `&lhs` are unchanged.

```metta
!(mork:join &lhs &rhs)
```

### Meet (intersection)

Keep only paths present in both `&lhs` and `&rhs`. Mutates `&lhs`.

```metta
!(mork:meet &lhs &rhs)
```

### Subtract (difference)

Remove from `&lhs` any path that exists in `&rhs`.

```metta
!(mork:subtract &lhs &rhs)
```

### Prefix Restrict (selector-shaped narrowing)

Keep only paths in `&lhs` that are admitted by a valued prefix in `&selector`. This is **not** intersection — `&selector` acts as a prefix selector, not an ordinary algebra operand.

```metta
!(mork:prefix-restrict &lhs &selector)
```

Clone when you want a filtered branch instead of modifying the original:

```metta
!(bind! &restricted (mork:clone &ws))
!(mork:prefix-restrict &restricted &selector)
```

### Branch-and-explore pattern

```metta
!(bind! &snapshot (mork:clone &ws))
!(mork:subtract &snapshot &noise)
!(mork:step! &snapshot 100)
; &ws is unchanged
```

Positive example:

- `mork:prefix-restrict` treats the right-hand side as a valued-prefix selector over encoded paths.

Negative example:

- it is not ordinary logical filtering and not “match this pattern against the left space”.

## 7. Path Introspection

`mork:path-of-atom` exposes the exact byte path used as the trie key.

```metta
!(bind! &p (mork:path-of-atom (edge a b)))
!(println! &p)
```

Positive example:

- use this when you want the actual machine encoding path.

Negative example:

- do not confuse it with a logical explanation or proof trace.

## 8. Zippers

A `mork:zipper` creates a snapshot-based read cursor over a workspace. The snapshot is taken at creation time, so the cursor sees a frozen view regardless of later mutations to the source workspace.

### Plain zipper — creation, navigation, and cleanup

```metta
!(bind! &z (mork:zipper &ws))
!(mork:zipper-descend-until! &z)
!(println! (mork:zipper-path-bytes &z))
!(println! (mork:zipper-path-exists &z))
!(println! (mork:zipper-is-val &z))
!(println! (mork:zipper-child-count &z))
!(println! (mork:zipper-child-bytes &z))
!(println! (mork:zipper-depth &z))
!(println! (mork:zipper-val-count &z))
!(mork:zipper-close! &z)
```

### Movement

```metta
!(mork:zipper-descend-byte! &z 42)          ; descend to child byte 42
!(mork:zipper-descend-first! &z)            ; descend to first child
!(mork:zipper-descend-last! &z)             ; descend to last child
!(mork:zipper-descend-index! &z 0)          ; descend to child at index 0
!(mork:zipper-descend-until! &z)            ; descend until a value or branch
!(mork:zipper-descend-until-max-bytes! &z 8); descend until value/branch or 8 bytes
!(mork:zipper-ascend! &z 1)                 ; ascend 1 step
!(mork:zipper-ascend-until! &z)             ; ascend to nearest value or branch
!(mork:zipper-ascend-until-branch! &z)      ; ascend to nearest branch point
!(mork:zipper-next-sibling-byte! &z)        ; move to next sibling at this level
!(mork:zipper-prev-sibling-byte! &z)        ; move to previous sibling
!(mork:zipper-next-step! &z)                ; depth-first step forward
!(mork:zipper-next-val! &z)                 ; advance to next valued path
!(mork:zipper-reset! &z)                    ; return to root
```

### Fork

```metta
!(bind! &zf (mork:zipper-fork &z))
; &zf is an independent cursor at the same position in the same snapshot
!(mork:zipper-close! &zf)
```

### Export from cursor focus

`mork:zipper-make-map` returns a fresh `MorkSpace` containing the structural subtrie below the focus. `mork:zipper-make-snapshot-map` does the same but also grafts the focus value (if any) onto the root `[]` of the returned map.

```metta
!(bind! &sub (mork:zipper-make-map &z))
!(bind! &snap (mork:zipper-make-snapshot-map &z))
```

Positive example:

- close cursors explicitly with `mork:zipper-close!`.
- use `mork:zipper-next-val!` to iterate all values without materializing the whole space.

Negative example:

- there is currently no matching public `mork:close-space!` for whole `MorkSpace` handles.

## 9. Product Zippers

Walk a stitched multi-factor search surface without materializing a brute-force join. Supports 2, 3, or 4 factors.

```metta
!(bind! &pz (mork:product-zipper &a &b &c))
!(mork:product-zipper-descend-until! &pz)
!(println! (mork:product-zipper-focus-factor &pz))
!(println! (mork:product-zipper-path-indices &pz))
!(println! (mork:product-zipper-factor-count &pz))
!(println! (mork:product-zipper-path-bytes &pz))
!(println! (mork:product-zipper-is-val &pz))
!(println! (mork:product-zipper-child-count &pz))
```

Product zippers support the same movement ops as plain zippers (`descend-byte!`, `descend-first!`, `descend-last!`, `ascend!`, `next-sibling-byte!`, `prev-sibling-byte!`, `next-step!`, `next-val!`, `reset!`, etc.) plus the product-specific `focus-factor`, `path-indices`, and `factor-count`.

```metta
!(mork:product-zipper-close! &pz)
```

## 10. Overlay Zippers

Inspect the virtual fused trie (`local <|> base` at each path) without materializing a join.

```metta
!(bind! &oz (mork:overlay-zipper &base &overlay))
!(mork:overlay-zipper-descend-until! &oz)
!(println! (mork:overlay-zipper-path-bytes &oz))
!(println! (mork:overlay-zipper-path-exists &oz))
!(println! (mork:overlay-zipper-is-val &oz))
!(println! (mork:overlay-zipper-child-count &oz))
```

Overlay zippers support the same movement ops as plain zippers (`descend-byte!`, `descend-first!`, `descend-last!`, `ascend!`, `next-sibling-byte!`, `prev-sibling-byte!`, `next-step!`, `reset!`, etc.) **except** `next-val!` and `val-count`, which the underlying overlay zipper does not expose.

```metta
!(mork:overlay-zipper-close! &oz)
```

Positive example:

- overlay zipper exposes the merged view without mutating either input workspace.

Negative example:

- it is not just a second ordinary space layered on top of the first one.

## 11. Recommended Demos

From the CeTTa build directory (where `./cetta` lives):

### Pure `mork` cursor/algebra demo

```bash
ulimit -v 6291456 && ./cetta --quiet examples/mork_showcase.metta
```

### Pure MM2 stepping demo

```bash
ulimit -v 6291456 && ./cetta --quiet examples/mork_mm2_showcase.metta
```

### Integrated MM2 + `mork` + MeTTa demo

```bash
ulimit -v 6291456 && ./cetta --quiet examples/mork_mm2_metta_showcase.metta
```

### Workspace algebra showcase

```bash
ulimit -v 6291456 && ./cetta --quiet examples/mork_algebra_showcase.metta
```

Demonstrates `mork:join`, `mork:meet`, `mork:subtract`, and `mork:clone` across the three canonical workflows: compose-then-derive, find shared/unique edges, and branch-and-explore without modifying the original.

### Counterexample loom demo

```bash
ulimit -v 6291456 && ./cetta --quiet examples/counterexample_loom.metta
```

This one is intentionally a finite candidate-comparison demo. It keeps live `MorkSpace` creation explicit at top level and exports visible atom sets into ordinary spaces for the catalog verdict.

## 12. Common Mistakes

### Using the low-level backend API instead of `mork:new-space`

`space-set-backend!` is a low-level runtime primitive for switching an existing
space's storage backend. For new MORK workspaces, prefer `mork:new-space`
which creates a properly configured `MorkSpace` directly.

Negative example:

```metta
!(bind! &s (new-space))
!(space-set-backend! &s mork)
```

Prefer:

```metta
!(bind! &ws (mork:new-space))
```

### Treating `MorkSpace` as generic `SpaceType`

Negative example:

```metta
!(add-atom &ws (edge a b))
!(match &ws (edge $x $y) ($x $y))
```

Use:

```metta
!(mork:add-atom &ws (edge a b))
!(mork:match &ws (edge $x $y) ($x $y))
```

### Forgetting cursor cleanup

Negative example:

```metta
!(bind! &z (mork:zipper &ws))
```

Prefer:

```metta
!(bind! &z (mork:zipper &ws))
!(mork:zipper-close! &z)
```

## 13. Current Surface Boundary

The intended mental model is:

- `MorkSpace` for live MM2 workspaces, workspace algebra, and expert PathMap/MORK inspection
- `mork:clone` for branching and persistence before mutating algebra
- ordinary spaces for ordinary CeTTa host-space workflows
- explicit exports and demos when crossing between them

Positive example:

- run MM2 in `MorkSpace`, compose workspaces with `mork:join`, branch with `mork:clone`, inspect exact encoded paths with zippers.

Negative example:

- do not assume every ordinary space operator has an invisible MORK meaning.
