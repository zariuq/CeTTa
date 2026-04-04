# `lib/mork.metta` How-To

This note is the practical guide to the explicit `mork:` lane in CeTTa.

The short version:

- load `lib/mork.metta`
- create a `MorkSpace` with `mork:new-space`
- use `mork:add-atom`, `mork:match`, `mork:get-atoms`, `mork:size`, and `mork:step!`
- use `mork:` algebra and cursor operators when you want machine-close PathMap/MORK behavior
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

No-duplicate add:

```metta
!(mork:add-atom-nodup &ws (edge a b))
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

Count:

```metta
!(println! (mork:size &ws))
```

Compatibility alias:

```metta
!(println! (mork:count-atoms &ws))
```

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

- `mork:join`, `mork:prefix-restrict`, zipper inspection, and the other read-side expert operators are **not** stepping operations.

## 5. ACT Dump, Load, And Open

Dump a live workspace to ACT:

```metta
!(mork:dump! &ws "../runtime/example.act")
```

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

## 6. Read-Side Algebra

These operators are structural PathMap/MORK algebra over `MorkSpace` values, and the result stays a fresh `MorkSpace`.

### Join

```metta
!(bind! &joined (mork:join &lhs &rhs))
```

### Meet

```metta
!(bind! &common (mork:meet &lhs &rhs))
```

### Subtract

```metta
!(bind! &diff (mork:subtract &lhs &rhs))
```

### Restrict

Preferred spelling:

```metta
!(bind! &restricted (mork:prefix-restrict &lhs &selector))
```

Compatibility alias:

```metta
!(bind! &restricted (mork:restrict &lhs &selector))
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

### Plain zipper

```metta
!(bind! &z (mork:zipper &ws))
!(mork:zipper-descend-until! &z)
!(println! (mork:zipper-path-bytes &z))
!(println! (mork:zipper-path-exists &z))
!(println! (mork:zipper-is-val &z))
!(mork:zipper-close! &z)
```

### Snapshot from the current focus

```metta
!(bind! &sub (mork:zipper-subspace &z))
```

This returns a fresh `MorkSpace` snapshot rooted at the current focus.

Positive example:

- close cursors explicitly with `mork:zipper-close!`.

Negative example:

- there is currently no matching public `mork:close-space!` for whole `MorkSpace` handles.

## 9. Product Zippers

Use these to walk a stitched multi-factor search surface without materializing a brute-force join.

```metta
!(bind! &pz (mork:product-zipper &a &b &c))
!(mork:product-zipper-descend-until! &pz)
!(println! (mork:product-zipper-focus-factor &pz))
!(println! (mork:product-zipper-path-indices &pz))
!(mork:product-zipper-close! &pz)
```

## 10. Overlay Zippers

Use these to inspect the virtual fused trie directly.

```metta
!(bind! &oz (mork:overlay-zipper &base &overlay))
!(mork:overlay-zipper-descend-until! &oz)
!(println! (mork:overlay-zipper-path-bytes &oz))
!(println! (mork:overlay-zipper-path-exists &oz))
!(println! (mork:overlay-zipper-is-val &oz))
!(mork:overlay-zipper-close! &oz)
```

Positive example:

- overlay zipper exposes the merged view without materializing `mork:join`.

Negative example:

- it is not just a second ordinary space layered on top of the first one.

## 11. Recommended Demos

From the CeTTa build directory (where `./cetta` lives):

### Pure `morkl` cursor/algebra demo

```bash
ulimit -v 6291456 && ./cetta --quiet examples/morkl_showcase.metta
```

### Pure MM2 stepping demo

```bash
ulimit -v 6291456 && ./cetta --quiet examples/mork_mm2_showcase.metta
```

### Integrated MM2 + `morkl` + MeTTa demo

```bash
ulimit -v 6291456 && ./cetta --quiet examples/morkl_mm2_metta_showcase.metta
```

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

- `MorkSpace` for live MM2 workspaces and expert PathMap/MORK inspection
- ordinary spaces for ordinary CeTTa host-space workflows
- explicit exports and demos when crossing between them

Positive example:

- run MM2 in `MorkSpace`, then inspect exact encoded paths with zippers.

Negative example:

- do not assume every ordinary space operator has an invisible MORK meaning.
