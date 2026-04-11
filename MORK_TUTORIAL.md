# MORK Tutorial

A practical guide to the `mork:` lane in CeTTa.

This guide assumes you broadly know MeTTa or PeTTa syntax, but **not**
necessarily MM2, MORK, or PathMap internals yet.

The short version:

- load `lib/mork.metta`
- create a `MorkSpace` with `mork:new-space`
- use `mork:add-atom`, `mork:match`, `mork:get-atoms`, `mork:size`, `mork:clone`, and `mork:step!`
- use `mork:` algebra as destination-mutating space operations, and use cursor operators when you want machine-close PathMap/MORK behavior
- do **not** treat `MorkSpace` as an ordinary generic `Space`

The stack, in one picture:

- **PathMap** is the trie substrate: atoms become encoded byte paths
- **MORK** is the explicit space/runtime layer built on that substrate
- **MM2** is the rule/execution model that runs inside a live `MorkSpace`
- **CeTTa `mork:`** is the user-facing way to access those capabilities from MeTTa

So if you are a MeTTa user, the important shift is:

- ordinary CeTTa spaces can be native or PathMap-backed
- `MorkSpace` gives you **direct access to MORK via MM2** — you write `exec` rules in the MM2 language, step them to fixpoint, and inspect the results
- the `mork:` surface additionally exposes **explicit PathMap algebra** (`join`, `meet`, `subtract`, `prefix-restrict`) and **trie cursor** operations that ordinary spaces don't surface

Further reading:

- [MORK wiki](https://github.com/trueagi-io/MORK/wiki)
- [Data in MORK](https://github.com/trueagi-io/MORK/wiki/Data-in-MORK)
- [MM2 Structuring Code](https://github.com/ClarkeRemy/MM2_Structuring_Code)

**Build prerequisites:** The `mork:` surface requires specific branches of MORK and PathMap. See the "MORK Bridge Prerequisites" section in `README.md` for exact branch/commit requirements.

**Why use MorkSpace?**

MorkSpace exposes capabilities that ordinary spaces don't surface:

- **MM2 exec stepping** — write forward-chaining `exec` rules in the MM2 language, step them to fixpoint inside the space
- **PathMap algebra** — `join`, `meet`, `subtract`, `prefix-restrict` operate directly on the trie
- **Structural sharing** — `mork:clone` is cheap (Arc refcounting on trie nodes), so branch-and-explore costs nearly nothing
- **ACT persistence** — dump a live space to a compiled binary format and reopen it instantly

If you just need `add-atom` / `match` / `remove-atom`, ordinary spaces work fine. MorkSpace is for when you need MM2 rules, algebra, or persistence.

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

## Quick Start

A complete workflow in 12 lines — create a space, add facts and a rule, derive, branch:

```metta
!(include mork)
!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))
!(mork:add-atom &ws
  (exec (0 hop) (, (edge $x $y) (edge $y $z)) (O (+ (path $x $z)))))
!(mork:step! &ws 4)                                     ; => 1
!(collapse (mork:match &ws (path $x $y) ($x $y)))      ; => ((a c))
!(bind! &branch (mork:clone &ws))
!(mork:remove-atom &branch (edge a b))
!(mork:step! &branch 4)                                 ; => 0 (rule was consumed)
!(collapse (mork:match &branch (path $x $y) ($x $y)))  ; => ((a c))
!(mork:size &ws)                                        ; => 3 (unchanged)
```

MM2 exec rules are consumed when they fire — the step derived `(path a c)` and removed the exec rule, so the space goes from `{edge a b, edge b c, exec ...}` to `{edge a b, edge b c, path a c}` — still 3 atoms.

That's it — create, populate, derive, branch. The rest of the tutorial explains each piece.

## 3. Basic Mutation And Query

```metta
!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))

!(mork:size &ws)                                        ; => 2
!(collapse (mork:match &ws (edge $x $y) ($x $y)))      ; => ((a b) (b c))
!(collapse (mork:get-atoms &ws))                        ; => ((edge a b) (edge b c))

!(mork:remove-atom &ws (edge a b))
!(mork:size &ws)                                        ; => 1
```

Bulk add:

```metta
!(mork:add-atoms &ws ((edge a b) (edge b c)))
```

### Clone

Clone creates a fresh snapshot sharing trie structure with the source. Mutations on the clone do not affect the original, and vice versa.

```metta
!(bind! &copy (mork:clone &ws))
!(mork:add-atom &copy (edge x y))
!(mork:size &copy)                                      ; => 2
!(mork:size &ws)                                        ; => 1 (unchanged)
```

### Important

Use `mork:` verbs, not generic space verbs — bare `add-atom`, `match`, etc. reject `MorkSpace` on purpose.

## 4. MM2 Exec Stepping

If you know MeTTa but not MM2 yet, the shortest useful picture is:

- you store ordinary facts in a `MorkSpace`
- you also store executable `exec` rules in that same space
- `mork:step!` asks the MM2 engine to advance that live space
- derived atoms are written back into that same `MorkSpace`

So MM2 is the forward-chaining rule layer of MORK, not a separate host language.

In CeTTa terms:

- `mork:add-atom` can add either passive data like `(edge a b)` or active rules like `(exec ...)`
- `mork:step!` performs up to a given number of MM2 execution steps and returns how many steps actually ran
- if no rule can fire, `mork:step!` returns `0`

An `exec` rule has this general shape:

```
(exec (PRIORITY NAME) PATTERN (O ACTIONS))
```

Read it like this:

- `(PRIORITY NAME)` identifies the rule
- `PATTERN` says what facts must be present
- `(O ACTIONS)` says what to add or remove when the pattern matches

`PATTERN` uses `(, ...)` for conjunction. `ACTIONS` use `(+ atom)` to add and `(- atom)` to remove.

This tiny example says:

- if `(edge X Y)` and `(edge Y Z)` are both present
- then add `(path X Z)`

```metta
!(bind! &ws (mork:new-space))

; Rule: when (edge X Y) and (edge Y Z) exist, derive (path X Z)
!(mork:add-atom &ws
  (exec (0 step)
    (, (edge $x $y)
       (edge $y $z))
    (O (+ (path $x $z)))))

!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge b c))

!(mork:step! &ws 4)                                     ; => 1 (one rule fired)
!(collapse (mork:match &ws (path $x $y) ($x $y)))      ; => ((a c))
!(mork:step! &ws 4)                                     ; => 0 (fixpoint reached)
```

Why this can look surprising at first:

- the rule itself is just another atom stored in the space
- MM2 exec stepping is not a separate hidden interpreter state; it advances the live `MorkSpace`
- that is why MM2 demos often print both ordinary facts and `exec` rules together before stepping

See `examples/mork_mm2_showcase.metta` for the runnable version.

For raw `.mm2` files at the CLI, use:

```bash
./cetta --lang mm2 examples/bc_socrates_pln.mm2
```

## 5. ACT Dump, Load, And Open

ACT (Atom-Compiled Trie) is MORK's binary persistence format. It serializes a live PathMap trie to disk so you can reload it without reparsing. Think of it as a compiled snapshot — fast to open, portable between sessions.

Dump a live space to ACT:

```metta
!(bind! &src (mork:new-space))
!(mork:add-atoms &src ((edge a b) (edge b c)))
!(mork:dump! &src "../runtime/example.act")             ; => ()
```

ACT faithfully preserves what ordinary loads, matches, and attached queries see. When a space carries extra duplicate/provenance metadata, CeTTa carries that through the ACT sidecar metadata as well.

Load ACT contents into an existing live space:

```metta
!(bind! &dst (mork:new-space))
!(mork:load-act! &dst "../runtime/example.act")         ; => ()
!(mork:size &dst)                                       ; => 2
!(collapse (mork:get-atoms &dst))                       ; => ((edge a b) (edge b c))
```

Open an attached compiled ACT directly as a `MorkSpace`:

```metta
!(bind! &compiled (mork:open-act "../runtime/example.act"))
!(get-type &compiled)                                   ; => MorkSpace
!(mork:size &compiled)                                  ; => 2
!(mork:match &compiled (edge a b) hit)                  ; => hit
!(mork:match &compiled (edge b c) hit)                  ; => hit
```

Representative round-trip:

```metta
!(bind! &compiled-clone (mork:clone &compiled))
!(mork:add-atom &compiled-clone (edge z y))
!(mork:size &compiled)                                  ; => 2 (attached source unchanged)
!(mork:size &compiled-clone)                            ; => 3
```

### Loading ACT into an ordinary space

You don't need a `MorkSpace` to read ACT data. Plain `import!` loads ACT atoms into an ordinary CeTTa space, queryable with generic space verbs:

```metta
!(import! &plain "../runtime/example.act")
!(size &plain)                                          ; => 2
!(match &plain (edge a b) hit)                          ; => hit
```

This is useful when you want to consume MORK-derived results from ordinary MeTTa code without touching the `mork:` API at all.

## 6. Algebra

All algebra ops mutate the left-hand (destination) space in place. The right-hand space is read but not modified. Clone first when you want persistence.

```metta
; Setup: two spaces with overlapping edges
!(bind! &a (mork:new-space))
!(mork:add-atoms &a ((edge a b) (edge b c) (edge c d)))
!(bind! &b (mork:new-space))
!(mork:add-atoms &b ((edge b c) (edge c d) (edge d e)))
```

### Join (union)

```metta
!(bind! &j (mork:clone &a))
!(mork:join &j &b)
!(collapse (mork:get-atoms &j))
; => ((edge a b) (edge b c) (edge c d) (edge d e))
```

### Meet (intersection)

```metta
!(bind! &m (mork:clone &a))
!(mork:meet &m &b)
!(collapse (mork:get-atoms &m))
; => ((edge b c) (edge c d))
```

### Subtract (difference)

```metta
!(bind! &s (mork:clone &a))
!(mork:subtract &s &b)
!(collapse (mork:get-atoms &s))
; => ((edge a b))
```

### Prefix Restrict (selector-shaped narrowing)

Keep only paths admitted by a valued prefix in the selector. This is **not** intersection — the selector acts as a prefix gate on the encoded trie.

```metta
!(bind! &sel (mork:new-space))
!(mork:add-atom &sel (edge b c))
!(bind! &r (mork:clone &a))
!(mork:prefix-restrict &r &sel)
!(collapse (mork:get-atoms &r))
; => ((edge b c))
```

### Branch-and-explore pattern

```metta
!(bind! &snapshot (mork:clone &a))
!(mork:subtract &snapshot &b)
!(mork:size &snapshot)                                  ; => 1
!(mork:size &a)                                         ; => 3 (unchanged)
```

See `examples/mork_algebra_showcase.metta` for a full runnable demo.

## 7. Path Introspection

`mork:path-of-atom` exposes the exact byte path used as the 256-radix trie key.

```metta
!(mork:path-of-atom (edge a b))
; => (3 196 101 100 103 101 193 97 193 98)
```

This is the raw machine encoding — not a logical explanation or proof trace.

How to read a byte path without over-reading it:

- treat the whole list as the exact trie key for that atom
- some bytes are recognizable printable text inside the serialization
- other bytes are structural/control bytes from the encoding
- in normal use, you compare paths and prefixes more often than you "decode" every byte by hand

For `(edge a b)` specifically:

- `101 100 103 101` are ASCII `e d g e`
- `97` is ASCII `a`
- `98` is ASCII `b`
- the remaining bytes are encoding markers, and you usually do not need to manipulate them manually

So the useful takeaway is usually not "memorize what `196` means," but:

- these two atoms share a long prefix
- this branch splits at the final byte
- this cursor is pointing at the encoded location of a known atom

Positive example:

- use `mork:path-of-atom` or `mork:zipper-path-bytes` when you want machine-close substrate inspection

Negative example:

- do not treat raw decimal byte lists as the normal authoring interface for ordinary MORK/MM2 work

---

## Advanced: Trie Inspection

Sections 8–10 cover zippers — snapshot-based read cursors over the raw PathMap trie. **Most MorkSpace users don't need zippers.** They're useful when you want to:

- Walk the trie structure without materializing all atoms (e.g., counting branches, finding shared prefixes)
- Inspect where two spaces agree or diverge at the byte level
- Build virtual views (overlay, product) for multi-space search without allocation

If you're getting started, skip ahead to Section 11 (Recommended Demos) and come back here when you need substrate-level inspection.

## 8. Zippers

A `mork:zipper` creates a snapshot-based read cursor over a space. The snapshot is taken at creation time, so the cursor sees a frozen view regardless of later mutations to the source space.

### Plain zipper — creation, navigation, and cleanup

Given a space with `(edge a b)` and `(edge a c)`, `descend-until!` walks to the first branch point — where the two atoms diverge:

```metta
!(bind! &ws (mork:new-space))
!(mork:add-atom &ws (edge a b))
!(mork:add-atom &ws (edge a c))
!(bind! &z (mork:zipper &ws))
!(mork:zipper-descend-until! &z)
!(mork:zipper-path-bytes &z)       ; => (3 196 101 100 103 101 193 97 193)
                                    ;    shared prefix of both edges
!(mork:zipper-path-exists &z)      ; => True   (node exists at this path)
!(mork:zipper-is-val &z)           ; => False  (not a leaf — it's a branch point)
!(mork:zipper-child-count &z)      ; => 2      (two children: b and c)
!(mork:zipper-child-bytes &z)      ; => (98 99)  (ASCII for 'b' and 'c')
!(mork:zipper-depth &z)            ; => 9      (9 bytes deep in the trie)
!(mork:zipper-val-count &z)        ; => 2      (2 valued paths below)
!(mork:zipper-close! &z)
```

### Movement

**Descend (go deeper into the trie):**

```metta
!(mork:zipper-descend-byte! &z 42)          ; descend to child byte 42
!(mork:zipper-descend-first! &z)            ; descend to first child
!(mork:zipper-descend-last! &z)             ; descend to last child
!(mork:zipper-descend-index! &z 0)          ; descend to child at index 0
!(mork:zipper-descend-until! &z)            ; descend until a value or branch
!(mork:zipper-descend-until-max-bytes! &z 8); descend until value/branch or 8 bytes
```

**Ascend (go back up):**

```metta
!(mork:zipper-ascend! &z 1)                 ; ascend 1 step
!(mork:zipper-ascend-until! &z)             ; ascend to nearest value or branch
!(mork:zipper-ascend-until-branch! &z)      ; ascend to nearest branch point
```

**Lateral (siblings at the same level):**

```metta
!(mork:zipper-next-sibling-byte! &z)        ; move to next sibling at this level
!(mork:zipper-prev-sibling-byte! &z)        ; move to previous sibling
```

**Iteration (depth-first traversal):**

```metta
!(mork:zipper-next-step! &z)                ; depth-first step forward
!(mork:zipper-next-val! &z)                 ; advance to next valued path
!(mork:zipper-reset! &z)                    ; return to root
```

Each returns `True`/`False` indicating whether the move succeeded, except `reset!` which always returns `()`.

Representative movement trace on the same two-edge space:

```metta
!(bind! &z2 (mork:zipper &ws))
!(mork:zipper-descend-until! &z2)           ; => True
!(mork:zipper-path-bytes &z2)               ; => (3 196 101 100 103 101 193 97 193)
!(mork:zipper-descend-first! &z2)           ; => True
!(mork:zipper-path-bytes &z2)               ; => (3 196 101 100 103 101 193 97 193 98)
!(mork:zipper-prev-sibling-byte! &z2)       ; => False  (already at first sibling)
!(mork:zipper-next-sibling-byte! &z2)       ; => True
!(mork:zipper-path-bytes &z2)               ; => (3 196 101 100 103 101 193 97 193 99)
!(mork:zipper-ascend-until-branch! &z2)     ; => True
!(mork:zipper-next-val! &z2)                ; => True
!(mork:zipper-path-bytes &z2)               ; => (3 196 101 100 103 101 193 97 193 98)
!(mork:zipper-next-val! &z2)                ; => True
!(mork:zipper-path-bytes &z2)               ; => (3 196 101 100 103 101 193 97 193 99)
!(mork:zipper-reset! &z2)                   ; => ()
!(mork:zipper-close! &z2)                   ; => ()
```

### Fork

```metta
!(bind! &z3 (mork:zipper &ws))
!(mork:zipper-descend-until! &z3)           ; => True
!(bind! &zf (mork:zipper-fork &z3))
; &zf is an independent cursor at the same position in the same snapshot
!(mork:zipper-path-bytes &zf)               ; => (3 196 101 100 103 101 193 97 193)
!(mork:zipper-descend-last! &zf)            ; => True
!(mork:zipper-path-bytes &zf)               ; => (3 196 101 100 103 101 193 97 193 99)
!(mork:zipper-close! &zf)                   ; => ()
!(mork:zipper-close! &z3)                   ; => ()
```

### Export from cursor focus

`mork:zipper-make-map` returns a fresh `MorkSpace` containing the structural subtrie below the focus. `mork:zipper-make-snapshot-map` does the same but also grafts the focus value (if any) onto the root `[]` of the returned map.

```metta
!(bind! &zroot (mork:zipper &ws))
!(bind! &sub (mork:zipper-make-map &zroot))
!(bind! &snap (mork:zipper-make-snapshot-map &zroot))
!(get-type &sub)                            ; => MorkSpace
!(get-type &snap)                           ; => MorkSpace
!(mork:size &sub)                           ; => 2
!(mork:size &snap)                          ; => 2
!(mork:zipper-close! &zroot)                ; => ()
```

At the root cursor in this example, both exports are the full two-edge space, so the visible result is the same. The distinction matters when the focus itself carries a value that should be grafted onto the returned root.

Positive example:

- close cursors explicitly with `mork:zipper-close!`.
- use `mork:zipper-next-val!` to iterate all values without materializing the whole space.

Negative example:

- there is currently no matching public `mork:close-space!` for whole `MorkSpace` handles.

## 9. Product Zippers

Walk a stitched multi-factor search surface without materializing a brute-force join. Supports 2, 3, or 4 factors.

```metta
!(bind! &a (mork:new-space))
!(bind! &b (mork:new-space))
!(bind! &c (mork:new-space))
!(mork:add-atom &a a)
!(mork:add-atom &b b)
!(mork:add-atom &c c)
!(bind! &pz (mork:product-zipper &a &b &c))
!(mork:product-zipper-factor-count &pz)     ; => 3
!(mork:product-zipper-child-count &pz)      ; => 1
!(mork:product-zipper-child-bytes &pz)      ; => (193)
!(mork:product-zipper-path-bytes &pz)       ; => ()
!(mork:product-zipper-descend-first! &pz)   ; => True
!(mork:product-zipper-path-bytes &pz)       ; => (193)
!(mork:product-zipper-descend-until! &pz)   ; => True
!(mork:product-zipper-focus-factor &pz)     ; => 0
!(mork:product-zipper-path-bytes &pz)       ; => (193 97)
!(mork:product-zipper-path-indices &pz)     ; => (2)
!(mork:product-zipper-descend-until! &pz)   ; => True
!(mork:product-zipper-focus-factor &pz)     ; => 1
!(mork:product-zipper-path-bytes &pz)       ; => (193 97 193 98)
!(mork:product-zipper-descend-until! &pz)   ; => True
!(mork:product-zipper-focus-factor &pz)     ; => 2
!(mork:product-zipper-is-val &pz)           ; => True
!(mork:product-zipper-val-count &pz)        ; => 1
!(mork:product-zipper-path-bytes &pz)       ; => (193 97 193 98 193 99)
```

Product zippers support the same movement ops as plain zippers (`descend-byte!`, `descend-first!`, `descend-last!`, `ascend!`, `next-sibling-byte!`, `prev-sibling-byte!`, `next-step!`, `next-val!`, `reset!`, etc.) plus the product-specific `focus-factor`, `path-indices`, and `factor-count`.

How to read this output:

- `factor-count = 3` means the product is stitching together three input spaces
- `focus-factor = 0` means the current frontier is being advanced by the first factor
- later `focus-factor = 1` means control has moved to the second factor
- the `path-bytes` here are a virtual product path, not a pretty surface atom

In other words, for product zippers the most important human-facing signals are usually:

- which factor is in focus
- how the focus changes as you descend
- whether the product frontier is a value or still branching

The raw bytes are still useful, but mainly if you are debugging or studying the substrate-level traversal.

```metta
!(mork:product-zipper-reset! &pz)           ; => ()
!(mork:product-zipper-close! &pz)           ; => ()
```

## 10. Overlay Zippers

An overlay zipper is a **read cursor over a virtual merged view of two spaces**.

The simplest mental model is:

- imagine the first space as the background
- imagine the second space laid on top of it
- now walk what you would see if both were visible at once

So an overlay zipper is useful when you want to **inspect the shape of "base plus overlay" without actually materializing a joined space**.

More precisely:

- it is read-only
- it exposes a union-shaped trie view
- if both inputs provide a value at the same path, the overlay side wins locally

That means it is best for questions like:

- "what branches become visible if I layer this selector or patch space on top?"
- "where do these two tries share structure?"
- "where do they diverge?"

### Case A: disjoint roots

Here `base` contributes the atom `a`, while `overlay` contributes `(edge a b)`. The overlaid view contains **both** top-level branches.

```metta
!(bind! &base (mork:new-space))
!(bind! &overlay (mork:new-space))
!(mork:add-atom &base a)
!(mork:add-atom &overlay (edge a b))
!(bind! &oz (mork:overlay-zipper &base &overlay))
!(mork:overlay-zipper-child-count &oz)      ; => 2
!(mork:overlay-zipper-child-bytes &oz)      ; => (3 193)
!(mork:overlay-zipper-path-bytes &oz)       ; => ()
!(mork:overlay-zipper-descend-index! &oz 0) ; => True
!(mork:overlay-zipper-path-bytes &oz)       ; => (3)    ; the (edge ...) branch
!(mork:overlay-zipper-path-exists &oz)      ; => True
!(mork:overlay-zipper-is-val &oz)           ; => False
!(mork:overlay-zipper-reset! &oz)           ; => ()
!(mork:overlay-zipper-descend-index! &oz 1) ; => True
!(mork:overlay-zipper-path-bytes &oz)       ; => (193)  ; the atom a branch
```

What to picture:

- at the root, the merged view has two children because the two spaces start with different top-level bytes
- byte `3` is the encoded head of `(edge ...)`
- byte `193` is the encoded symbol branch for `a`

So an overlay zipper is not "look at base, then look at overlay." It is walking the **single virtual trie you would get if both were visible at once**.

### Case B: shared prefix, then divergence

This is often the more useful example.

```metta
!(bind! &lhs (mork:new-space))
!(bind! &rhs (mork:new-space))
!(mork:add-atom &lhs (edge a c))
!(mork:add-atom &rhs (edge a b))
!(bind! &shared (mork:overlay-zipper &lhs &rhs))
!(mork:overlay-zipper-child-count &shared)        ; => 1
!(mork:overlay-zipper-descend-until! &shared)     ; => True
!(mork:overlay-zipper-path-bytes &shared)
; => (3 196 101 100 103 101 193 97 193)
!(mork:overlay-zipper-path-exists &shared)        ; => True
!(mork:overlay-zipper-child-count &shared)        ; => 2
!(mork:overlay-zipper-is-val &shared)             ; => False
!(mork:overlay-zipper-depth &shared)              ; => 9
```

What this means:

- both spaces agree on the prefix `(edge a _)`
- so the overlay zipper walks that shared path as one fused branch
- it stops exactly where the spaces diverge: one child for `b`, one child for `c`

This is the key benefit over materializing a join just to inspect it: you can examine the merged frontier directly, including where two tries line up and where they split.

If you want the shortest honest summary:

- `mork:join` mutates a destination space to actually combine content
- `mork:overlay-zipper` lets you inspect that combined view without creating that destination space

Overlay zippers support the same movement ops as plain zippers (`descend-byte!`, `descend-first!`, `descend-last!`, `ascend!`, `next-sibling-byte!`, `prev-sibling-byte!`, `next-step!`, `reset!`, etc.) **except** `next-val!` and `val-count`, which the underlying overlay zipper does not expose.

```metta
!(mork:overlay-zipper-reset! &oz)           ; => ()
!(mork:overlay-zipper-next-step! &oz)       ; => True
!(mork:overlay-zipper-path-bytes &oz)       ; => (3)
!(mork:overlay-zipper-next-sibling-byte! &oz) ; => True
!(mork:overlay-zipper-path-bytes &oz)       ; => (193)
!(mork:overlay-zipper-close! &oz)           ; => ()
```

Positive example:

- overlay zipper exposes the merged view without mutating either input space
- use it when you want to inspect "what would be visible if overlay were laid on top of base?" without allocating a joined workspace

Negative example:

- it is not just "look at base, then look at overlay"
- it is not a destination-mutating algebra op like `mork:join`

## 11. Runnable Demos

### MM2 exec stepping

```bash
./cetta --quiet examples/mork_mm2_showcase.metta
```

Two edges, one exec rule, step to fixpoint:

```text
=== MM2 stepping ===
("before" ((edge a b) (edge b c) (exec (0 step) (, (edge $a $b) (edge $b $c)) (O (+ (path $a $c))))))
("step-1" 1)
("after" ((edge a b) (edge b c) (path a c)))
("derived" ((a c)))
("step-2-fixpoint" 0)
=== showcase complete ===
```

The rule fired once, derived `(path a c)`, then a second step found nothing new.

### Algebra

```bash
./cetta --quiet examples/mork_algebra_showcase.metta
```

Two transit route networks — meet finds shared segments, subtract finds unique ones, join merges and derives reachability:

```text
=== meet: shared backbone ===
("backbone" ((route bravo charlie) (route charlie delta)))
=== subtract: unique segments ===
("north-only" ((route alpha bravo)))
("south-only" ((route echo foxtrot) (route delta echo)))
```

The later branch-and-explore section shows why `mork:clone` matters before mutating.

### Integrated MM2 + MeTTa

```bash
./cetta --quiet examples/mork_mm2_metta_showcase.metta
```

MM2 derives a fact, then ordinary MeTTa equations annotate it with semantic metadata:

```text
=== derive a path via MM2 ===
("before" ((edge a b) (edge b c) (exec (0 step) (, (edge $a $b) (edge $b $c)) (O (+ (path $a $c))))))
("steps" 1)
("derived" ((a c)))
=== substrate encoding ===
("path-a-c-bytes" (3 196 112 97 116 104 193 97 193 99))
=== prefix-restrict: only derived paths ===
("filtered" ((path a c)))
=== zipper on filtered space ===
("at" (3 196 112 97 116 104 193 97 193 99) "is-val?" True)
=== overlay: filtered + selector ===
("at" (3 196 112 97 116 104 193 97 193 99) "is-val?" True)
=== MeTTa analysis ===
("annotated" ((annotated a c strong (two-hop a b c))))
=== showcase complete ===
```

### Counterexample loom

```bash
./cetta --quiet examples/counterexample_loom.metta
```

MeTTa searches over MM2 rule subsets; MORK saturates each candidate independently. See Section 13 for the full walkthrough.

```text
=== Counterexample Loom ===
(legend solar/lunar=require star-lantern, sterile=no ash, chorus-guard=no chorus)
(candidate-check solar-only   solar True  lunar False  sterile True  chorus-guard True)
(candidate-check chorus-trap  solar False lunar False  sterile True  chorus-guard False)
(candidate-check balanced     solar False lunar True   sterile True  chorus-guard True)
(catalog-result no-shared-bundle)
```

No single bundle passes all scenarios — that's the answer.

### Substrate inspection (advanced)

```bash
./cetta --quiet examples/mork_showcase.metta
```

Exercises zippers, prefix-restrict, overlay, and product cursors on a two-edge space:

```text
=== MorkSpace basics ===
("type" MorkSpace)
("atoms" ((edge a b) (edge a c)))
("match edge a _" (b c))
("size" 2)
=== encoded path of (edge a b) ===
("path-of" (edge a b) (3 196 101 100 103 101 193 97 193 98))
=== zipper: walk to first branch/value ===
("root-children" 1 "child-bytes" (3))
("shared-prefix-bytes" (3 196 101 100 103 101 193 97 193))
("exists?" True "is-val?" False)
("first-leaf-bytes" (3 196 101 100 103 101 193 97 193 98) "is-val?" True)
=== prefix-restrict: keep only (edge a b) ===
("restricted" ((edge a b)))
=== overlay: restricted + selector ===
("overlay-focus-bytes" (3 196 101 100 103 101 193 97 193 98) "is-val?" True)
=== product: three-factor traversal ===
("factors" 3)
("focus-factor" 0 "product-path-bytes" (2 193 120 193 49))
("focus-factor" 1 "product-path-bytes" (2 193 120 193 49 2 193 121 193 50))
=== showcase complete ===
```

Read that output in this order:

- start with the atom-level lines: `atoms`, `match`, `restricted`
- then read `path-of` and `shared-prefix-bytes` as substrate evidence for those same atoms
- treat the final overlay/product lines as advanced cursor inspection, not as the ordinary authoring interface

## 12. Common Mistakes

### Using the low-level backend API instead of `mork:new-space`

`space-set-backend!` is a low-level runtime primitive for switching an existing
space's storage backend. For new MORK spaces, prefer `mork:new-space`
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

**What happens if you forget:** The cursor's native handle leaks. CeTTa has no finalizer infrastructure for native resources, so the snapshot memory stays allocated until the process exits. In a long-running session with many cursors, this is a real memory leak — not a crash, but a silent resource drain.

## 13. Walkthrough: Counterexample Loom

`examples/counterexample_loom.metta` is the most non-trivial MORK demo. It uses MM2 as a **saturation oracle** inside a MeTTa combinatorial search — the kind of workflow MORK was designed for.

### The problem

Eight MM2 rules define two derivation chains and two decoys:

```
Solar path:  ember+dew → mist → mist+seed → sun-bloom → sun-bloom+echo → star-lantern
Lunar path:  dew+echo → dream-mist → dream-mist+seed → moon-veil → moon-veil+spark → star-lantern
Decoys:      seed+echo → chorus (unwanted),  ember+spark → ash (unwanted)
```

The goal: find a subset of rules that produces `(artifact star-lantern)` in every test scenario while never producing forbidden artifacts like `(artifact chorus)` or `(artifact ash)`.

### The search structure

Three candidate bundles select different rule subsets:

```metta
(= (candidate-bundle solar-only)     ; solar chain only, no lunar, no decoys
  (bundle solar-starter solar-middle solar-terminal skip skip skip skip skip))

(= (candidate-bundle chorus-trap)    ; all 6 real rules + chorus decoy
  (bundle solar-starter solar-middle solar-terminal
         lunar-starter lunar-middle lunar-terminal decoy-chorus skip))

(= (candidate-bundle balanced)       ; both chains, no decoys
  (bundle solar-starter solar-middle solar-terminal
         lunar-starter lunar-middle lunar-terminal skip skip))
```

Four test scenarios provide different starting materials:

- **solar**: ember, dew, seed, echo — feeds the solar chain
- **lunar**: dew, echo, seed, spark — feeds the lunar chain
- **sterile**: ember, spark — not enough for any chain
- **chorus-guard**: seed, echo — could trigger the chorus decoy

### How MORK fits in

For each (candidate × scenario) pair, the loom:

1. Creates a fresh `MorkSpace`
2. Loads the candidate's rules via `match &self (rule $label $atom)` → `mork:add-atom`
3. Loads the scenario's starting materials
4. Runs `mork:step! &ws 64` — MM2 saturates to fixpoint
5. Exports the visible atoms to an ordinary CeTTa space for inspection

```metta
(= (prepare-trial! $ws $name $scenario)
  (let* (($_ (load-bundle! $ws $name))
         ($_ (load-substrate! $ws $scenario))
         ($_ (mork:step! $ws 64)))
    ()))
```

This creates **12 MorkSpaces** (3 candidates × 4 scenarios), each independently saturated.

### The verdict

MeTTa inspects the exported ordinary spaces:

```metta
!(println! (candidate-check solar-only
             solar   (view-has? &solarOnlySolarView   (artifact star-lantern))
             lunar   (view-has? &solarOnlyLunarView   (artifact star-lantern))
             sterile (forbidden-absent? &solarOnlySterileView (artifact ash))
             chorus  (forbidden-absent? &solarOnlyChorusView  (artifact chorus))))
```

Output:

```
(candidate-check solar-only   solar True  lunar False  sterile True  chorus-guard True)
(candidate-check chorus-trap  solar False lunar False  sterile True  chorus-guard False)
(candidate-check balanced     solar False lunar True   sterile True  chorus-guard True)
(catalog-result no-shared-bundle)
```

- **solar-only** passes solar but fails lunar (no lunar rules)
- **chorus-trap** fails solar AND lunar (decoy chorus rule interferes)
- **balanced** passes lunar but fails solar (rule interaction)
- **No bundle works for all scenarios** — that's the real answer

The final search uses `superpose` to nondeterministically check all candidates:

```metta
!(let $winner
     (select (superpose (
       (winner-if-ok solar-only ...)
       (winner-if-ok chorus-trap ...)
       (winner-if-ok balanced ...))))
   (println! (if (== $winner Empty)
               (catalog-result no-shared-bundle)
               $winner)))
```

### What this demonstrates

- **MORK as oracle**: each MorkSpace runs MM2 saturation independently — MeTTa doesn't need to know the derivation details
- **Multi-space composition**: 12 independent spaces, each with different rule/data combinations
- **MeTTa + MORK boundary**: rules stored as MeTTa data atoms, loaded into MORK for execution, results exported back to ordinary spaces for inspection
- **Combinatorial search**: MeTTa's `superpose` drives the hypothesis space, MORK drives the deduction

```bash
./cetta --quiet examples/counterexample_loom.metta
```

## 14. Current Surface Boundary

The intended mental model is:

- `MorkSpace` for live MM2 spaces, algebra, and expert PathMap/MORK inspection
- `mork:clone` for branching and persistence before mutating algebra
- ordinary spaces for ordinary CeTTa host-space workflows
- explicit exports when crossing between them
