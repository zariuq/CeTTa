#!/usr/bin/env bash
set -euo pipefail

STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
NAME="rho${STAMP}_semantic_substitution_review"
ROOT="/home/zar/claude/review_bundles/${NAME}"
ZIP="/home/zar/claude/review_bundles/${NAME}.zip"
HASH="${ZIP}.sha256"

SRC_CETTA="/home/zar/claude/hyperon/cetta-hyperpose"
SRC_LEAN="/home/zar/claude/lean-projects/mettapedia"
SRC_ALGORITHMS="/home/zar/claude/lean-projects/algorithms"
SRC_METTAIL_CORE="/home/zar/claude/lean-projects/batteries/mettail-core"
SRC_GF_CORE="/home/zar/claude/lean-projects/batteries/gf-core"
SRC_MM_LEAN4="/home/zar/claude/hyperon/metamath/mm-lean4"
SRC_ROOT="/home/zar/claude"
LIT="/home/zar/claude/literature/Programming_Languages_and_Semantics/Rho_Calculus_and_OSLF"

export STAMP NAME ROOT

if [ -e "$ROOT" ] || [ -e "$ZIP" ] || [ -e "$HASH" ]; then
  echo "Refusing to overwrite existing bundle path: $ROOT" >&2
  exit 1
fi

mkdir -p "$ROOT"/{cetta-rho-review,mettapedia-rho-snapshot,papers,logs,algorithms,batteries,hyperon/metamath}

rsync -a \
  --exclude='.git' \
  --exclude='build/' \
  --exclude='target/' \
  --exclude='*.o' \
  --exclude='*.d' \
  --exclude='*.olean' \
  --exclude='*.ilean' \
  --exclude='.he-logs/' \
  --exclude='__pycache__/' \
  --exclude='.git/' \
  "$SRC_CETTA"/ "$ROOT/cetta-rho-review/"

rsync -a \
  --exclude='.git' \
  --exclude='.git/' \
  --exclude='.lake/' \
  --exclude='build/' \
  --exclude='*.olean' \
  --exclude='*.ilean' \
  --exclude='.tmp*' \
  "$SRC_LEAN"/ "$ROOT/mettapedia-rho-snapshot/"

rsync -a \
  --exclude='.git/' \
  --exclude='.lake/' \
  --exclude='build/' \
  --exclude='*.olean' \
  --exclude='*.ilean' \
  "$SRC_ALGORITHMS"/ "$ROOT/algorithms/"

rsync -a \
  --exclude='.git/' \
  --exclude='.lake/' \
  --exclude='build/' \
  --exclude='*.olean' \
  --exclude='*.ilean' \
  "$SRC_METTAIL_CORE"/ "$ROOT/batteries/mettail-core/"

rsync -a \
  --exclude='.git/' \
  --exclude='.lake/' \
  --exclude='build/' \
  --exclude='*.olean' \
  --exclude='*.ilean' \
  "$SRC_GF_CORE"/ "$ROOT/batteries/gf-core/"

rsync -a \
  --exclude='.git/' \
  --exclude='.lake/' \
  --exclude='build/' \
  --exclude='*.olean' \
  --exclude='*.ilean' \
  "$SRC_MM_LEAN4"/ "$ROOT/hyperon/metamath/mm-lean4/"

python3 - <<'PY'
from pathlib import Path
import os

lakefile = Path(os.environ["ROOT"]) / "mettapedia-rho-snapshot" / "lakefile.toml"
text = lakefile.read_text(encoding="utf-8")
old = '../../hyperon/metamath/mm-lean4'
new = '../hyperon/metamath/mm-lean4'
if old not in text:
    raise SystemExit(f"expected dependency path not found in {lakefile}")
lakefile.write_text(text.replace(old, new), encoding="utf-8")

manifest = Path(os.environ["ROOT"]) / "mettapedia-rho-snapshot" / "lake-manifest.json"
manifest_text = manifest.read_text(encoding="utf-8")
if old not in manifest_text:
    raise SystemExit(f"expected dependency path not found in {manifest}")
manifest.write_text(manifest_text.replace(old, new), encoding="utf-8")
PY

if [ -d "$SRC_LEAN/.lake/packages" ]; then
  mkdir -p "$ROOT/mettapedia-rho-snapshot/.lake"
  ln -s "$SRC_LEAN/.lake/packages" "$ROOT/mettapedia-rho-snapshot/.lake/packages"
fi

python3 - <<'PY'
from pathlib import Path
import os
from textwrap import dedent

root = Path(os.environ["ROOT"])
stamp = os.environ["STAMP"]
name = os.environ["NAME"]

def write(rel: str, text: str) -> None:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(dedent(text).lstrip(), encoding="utf-8")

write(
    "README.md",
    """
    # rho Semantic Substitution Review Bundle

    Snapshot: __STAMP__
    Bundle name: __NAME__

    ## Purpose

    Outside review of the strict-core rho semantic-substitution alignment between:

    - CeTTa's public `--lang rhocalc` reducer in `cetta-rho-review/`
    - the Lean 4 formalization in `mettapedia-rho-snapshot/`
    - the Meredith-Radestock 2005 and Meredith/Stay categorical papers in `papers/`

    The focused review question is no longer "how do we implement semantic substitution?".
    That part is already live and tested. The real open question is:

    What is the correct **global type-preservation theorem** for semantic COMM substitution,
    once paper-faithful name normalization and quote opacity are taken seriously?

    ## Key Files To Read First

    1. `REVIEW_CONTEXT.md`
    2. `GPT_REVIEW_PROMPT.md`
    3. `progress.md`
    4. `papers/Meredith_Radestock_2005_A_Reflective_Higher_Order_Calculus.pdf`
    5. `mettapedia-rho-snapshot/Mettapedia/Languages/ProcessCalculi/RhoCalculus/SemanticSubstitution.lean`
    6. `mettapedia-rho-snapshot/Mettapedia/Languages/ProcessCalculi/RhoCalculus/Reduction.lean`
    7. `mettapedia-rho-snapshot/Mettapedia/Languages/ProcessCalculi/RhoCalculus/Soundness.lean`
    8. `mettapedia-rho-snapshot/Mettapedia/OSLF/Framework/BeckChevalleyOSLF.lean`
    9. `cetta-rho-review/src/rhocalc_core.c`
    10. `cetta-rho-review/tests/rhocalc_lean_microcheck.lean`

    ## Build / Reproduction

    CeTTa:

    ```bash
    cd cetta-rho-review
    METTAPEDIA_ROOT=../mettapedia-rho-snapshot make -j1 BUILD=core test-rhocalc
    ```

    Lean:

    ```bash
    cd mettapedia-rho-snapshot
    lake build Mettapedia.Languages.ProcessCalculi.RhoCalculus.SemanticSubstitution
    lake build Mettapedia.Languages.ProcessCalculi.RhoCalculus
    lake build Mettapedia.OSLF.Framework.BeckChevalleyOSLF
    ```

    Cross-check:

    ```bash
    cd ../cetta-rho-review
    python3 scripts/rhocalc_lean_microcheck.py \
      ../mettapedia-rho-snapshot \
      tests/rhocalc_lean_microcheck.lean
    ```

    ## Important Packaging Note

    To keep the bundle reviewable in size, this snapshot intentionally omits:

    - Lean dependency caches under `.lake/`
    - bundle-local build artifacts
    - CeTTa build products under `build/`

    The bundle **does** vendor the local Lean path dependencies required by `mettapedia`:

    - `algorithms/`
    - `batteries/mettail-core/`
    - `batteries/gf-core/`
    - `hyperon/metamath/mm-lean4/`

    The source snapshot was verified locally before zipping. If a reviewer rebuilds the Lean
    snapshot elsewhere, `lake` may need to restore dependencies from `lake-manifest.json`.
    The final archive omits `.lake/`; the source-machine verification may reuse an existing
    local Lean package cache only as a temporary build aid.

    ## Scope

    In scope:

    - paper-faithful semantic substitution for strict-core rho COMM
    - exact-vs-quotiented type preservation under semantic normalization
    - the Beck-Chevalley / typed-at consequences of that theorem shape
    - the strict-core `rhocalc` + `lib/rho.metta` foundation as a public kernel

    Out of scope:

    - Rholang-only joins/contracts/persistence
    - scheduler fairness design beyond already-documented policy hygiene
    - `rho:step` / debug/meta surfaces
    - unrelated HE/PeTTa/CeTTa runtime topics

    ## Logs

    Verification logs are in `logs/`.
    Workspace branch / status / build results are in `workspace_state.txt`.
    """
    .replace("__STAMP__", stamp)
    .replace("__NAME__", name),
)

write(
    "progress.md",
    """
    # Progress Since rho20260522T132158Z

    ## Scope

    This bundle covers the semantic-substitution alignment slice for strict-core rho:

    - CeTTa public reducer
    - Lean operational semantics
    - Lean typing / Beck-Chevalley layer
    - CeTTa↔Lean theorem-backed cross-check fixtures

    ## Decision Log

    1. **Removed free top-level DROP from Lean `Reduces`.**

       Earlier Lean had a free top-level `DROP` reduction that did not match Meredith-Radestock 2005.
       Free drop is now inert in the strict-core relation unless semantic substitution reveals it.

    2. **Added a paper-faithful semantic substitution module in Lean.**

       `SemanticSubstitution.lean` now mirrors the CeTTa C reducer's matched-drop behavior:

       - quoted code is opaque to substitution
       - names normalize through quote-drop
       - matched drop-of-quote collapses during COMM substitution

    3. **Swapped the live Lean strict-core operational layer to semantic substitution.**

       The active rho files now reduce with `semanticCommSubst`, not the old syntactic opener.
       This includes the one-step relation, executable engine, context/LTS layer, present-moment
       tooling, and strict-core bridge code.

    4. **Proved exact semantic typing only on the agreement fragment.**

       `comm_preserves_type_semantic_of_agreement` and
       `comm_beck_chevalley_semantic_of_agreement` are the honest exact theorems:
       if semantic and syntactic substitution compute the same residual, the old exact predicate
       theorem transfers.

    5. **Made the semantic pullback theorem explicit without overclaiming.**

       `commPbSemantic_typedAt` is purely definitional:

       semantic pullback of `typedAt` unfolds to typing of `semanticCommSubst`.

       It does **not** claim exact fiber preservation.

    6. **Proved both positive agreement and negative divergence witnesses.**

       The Lean microcheck now has:

       - positive common-fragment agreement witnesses
       - negative divergence witnesses for:
         - bound-drop collapse
         - quote opacity
         - whole-name quote-drop normalization

    7. **Mathematical discovery: exact predicate preservation under semantic substitution is false in general.**

       This is the key result of the slice.

       Semantic substitution performs paper-faithful normalization that can change the syntactic
       shape of names and quoted positions. Arbitrary fiber predicates can distinguish the pre- and
       post-normalized forms, so the old theorem shape is not merely hard to prove — it fails.

    8. **The live operational semantics is now ahead of the theorem layer on one question only:**

       What is the right global theorem replacing exact `typedAt` preservation?

       Current honest options:

       - **A:** exact preservation only on the agreement fragment
       - **B:** global sort-only preservation plus agreement-fragment refinement
       - **C:** quotient / modulo-`≡_N` typing as the long-term exact theorem
       - **D:** another better formulation, if outside review suggests one

    ## Current Recommendation

    Ship / reason with:

    - live semantic operational semantics,
    - exact agreement-fragment theorem,
    - explicit negative witnesses,
    - and treat quotient typing modulo name-equivalence as the likely long-term theorem shape.

    ## Test / Build Status

    Verified from the bundle snapshot before zipping:

    - CeTTa `test-rhocalc`
    - Lean `SemanticSubstitution`
    - Lean rho umbrella
    - Lean `BeckChevalleyOSLF`
    - CeTTa↔Lean microcheck
    """
)

write(
    "REVIEW_CONTEXT.md",
    """
    # Review Context

    ## Overview

    This bundle is about one narrow but foundational question in the strict-core rho implementation:

    When COMM uses the paper-faithful **semantic substitution** of Meredith-Radestock 2005,
    what is the right global typing theorem?

    The implementation side is in good shape. The remaining decision is mathematical:
    exact fiber-predicate preservation is false in general, so the theorem layer must either weaken,
    quotient, or otherwise reformulate what "type preservation" means.

    ## Overall Goal

    CeTTa's `--lang rhocalc` and `lib/rho.metta` together are intended to be a strict-core,
    paper-faithful foundation for rho-calculus work.

    The intended contract is:

    1. **Paper-faithful semantics.**
       Only strict-core constructors belong in the kernel:
       `rho:nil`, `rho:par`, `rho:send`, `rho:recv`, `rho:quote`, `rho:drop`.

    2. **Lean-verified relation.**
       The Lean reduction relation should match the strict-core operational relation that CeTTa runs.

    3. **Evaluator policy kept separate.**
       Scheduler choice and reduction limits are evaluator controls, not object-language features.

    4. **No object-language operational introspection.**
       No `rho:step`, frontier oracle, or hidden CLI/debug channel belongs in strict-core rho.

    5. **`lib/rho.metta` stays austere.**
       Strict-core constructor/surface support only. Rholang-only sugar belongs elsewhere.

    The semantic-substitution theorem question is the last major strict-core semantic seam before the
    foundation can be treated as settled.

    ## Paper Background

    The canonical reference is Meredith & Radestock 2005:

    - §2.4: quote/drop interaction and name equivalence
    - §2.5: syntactic substitution
    - §2.7: semantic substitution
    - §2.8: reduction relation

    The crucial distinction is:

    - syntactic substitution is a locally nameless / alpha-management device
    - semantic substitution is the computational engine of COMM

    Two paper-mandated facts matter here:

    1. **Free drop is inert** unless substitution reveals the quoted process under it.
    2. **Quoted code is opaque** to substitution.

    These are not optional engineering choices; they are part of the paper's intended semantics.

    ## CeTTa Implementation

    The CeTTa C reducer implements semantic substitution in `src/rhocalc_core.c`, especially the
    `RHO_DROP` substitution case around line 904:

    - names are normalized
    - the substitution tracks whether a bound-name match really happened
    - only a matched drop-of-quote collapses to the quoted process

    This means the public reducer is already on the paper-faithful side.

    Public surface:

    - `--lang rhocalc`
    - `--rho-reduction-limit`
    - `--rho-scheduler`

    No hidden debug flags are part of this story.

    ## Lean Implementation

    The Lean side now mirrors that semantic substitution in
    `Mettapedia/Languages/ProcessCalculi/RhoCalculus/SemanticSubstitution.lean`.

    The live strict-core operational files already use `semanticCommSubst`.
    This is not a dormant target module; it is the active operational semantics.

    Key proved properties:

    - matched bound-drop collapse
    - literal static drop preserved
    - quote opacity preserved
    - whole-name quote-drop normalization
    - explicit divergence from the older syntactic `openBVar`-based substitution

    ## What Broke And Why

    The old exact theorem shape was:

    > body has predicate `φ` in the extended context
    > argument has process type in the base context
    > therefore COMM residual has exact predicate `φ` in the base context

    That is valid for the older syntactic opener on its own terms.
    It is **not** valid globally for semantic substitution.

    Why?

    Because semantic substitution performs paper-faithful normalization that changes syntactic shape:

    - `⌜*x⌝` can normalize to `x`
    - quoted bodies are left opaque instead of being traversed syntactically

    An arbitrary fiber predicate can distinguish those two shapes.
    So exact predicate preservation fails for some terms.

    This is now witnessed constructively in the Lean microcheck.

    ## Current Honest Theorem Layer

    The theorem layer currently says three things:

    1. **Exact theorem on the agreement fragment.**
       If semantic and syntactic substitution compute the same residual, the old exact theorem transfers.

    2. **Definitional semantic pullback theorem.**
       Semantic pullback of `typedAt` unfolds exactly to typing of `semanticCommSubst`.

    3. **Negative divergence witnesses.**
       Some terms provably do *not* lie in the agreement fragment.

    This is honest and useful, but it is not the final global theorem.

    ## Review Question

    What is the right replacement for exact global predicate preservation?

    The main candidate directions are:

    - **Sort-only preservation** globally, with exact agreement-fragment refinement
    - **Typing modulo name-equivalence** (`≡_N`) as the true exact theorem
    - **Some richer categorical/predicate saturation notion** that makes the normalization invisible

    ## Why Outside Review Helps Now

    This is a good review point because the implementation and counterexamples are already concrete.
    We are not asking reviewers to debug code; we are asking them to judge the right theorem shape.
    """
)

write(
    "GPT_REVIEW_PROMPT.md",
    """
    # Review Request: Global Type Preservation Under Semantic COMM Substitution in Strict-Core rho

    ## Context

    We are formalizing the Meredith-Radestock 2005 reflective higher-order rho-calculus in Lean 4,
    with CeTTa as a public reducer and Mettapedia as the theorem layer.

    The operational semantics is already live and paper-faithful on the COMM substitution question:

    - CeTTa C reducer: `cetta-rho-review/src/rhocalc_core.c`
    - Lean semantic substitution: `mettapedia-rho-snapshot/Mettapedia/Languages/ProcessCalculi/RhoCalculus/SemanticSubstitution.lean`
    - Lean live operational files already use `semanticCommSubst`

    The paper background is Meredith-Radestock 2005:

    - §2.4 quote/drop and name equivalence
    - §2.5 syntactic substitution
    - §2.7 semantic substitution
    - §2.8 reduction

    The key paper-faithful features are:

    1. free drop is inert unless substitution reveals the quote under it
    2. quoted code is opaque to substitution
    3. names normalize through quote-drop equivalence

    ## The Problem

    The old exact typing theorem shape was built around the locally nameless syntactic opener:

    - extend the context by a bound name
    - type the opened body
    - exact same fiber predicate `φ` is preserved after substitution

    We now know that this exact theorem shape is false in general for semantic substitution.

    Why it fails:

    - semantic substitution can normalize `⌜*x⌝` to `x`
    - semantic substitution does not descend into quoted code
    - arbitrary fiber predicates can distinguish these syntactic shapes

    This is not a proof-engineering accident; it is a mathematical consequence of the paper-faithful semantics.

    ## What Is Already Proved

    Please assume the following is settled and inspect the bundle if you want the exact proofs:

    - `semanticCommSubst` is implemented and theorem-backed
    - the live Lean operational relation already uses it
    - exact semantic typing is proved on the **agreement fragment**
      where `semanticCommSubst p q = commSubst p q`
    - semantic pullback of `typedAt` is unfolded definitionally
    - constructive divergence witnesses exist for:
      - bound-drop collapse
      - quote opacity
      - whole-name quote-drop normalization
    - CeTTa public reducer + Lean microcheck cross-validation passes

    ## The Review Question

    What is the cleanest **globally provable** replacement for exact predicate preservation?

    Candidate directions:

    ### A. Sort-only preservation

    Prove globally that semantic COMM substitution preserves the `Proc` sort,
    but do not claim exact fiber-predicate preservation except on the agreement fragment.

    ### B. Typing modulo name equivalence

    Make `≡_N` explicit in Lean, then define a quotient-style typing judgment
    where exact preservation is restored modulo name equivalence / semantic normalization.

    ### C. Some richer saturation / categorical closure

    Replace exact fiber predicates with a saturated notion that already respects the
    relevant operational/name equivalences.

    ### D. Another better formulation

    If there is a clearer theorem shape from prior art, that is what we want.

    ## Specific Questions

    1. Which theorem shape is the right one for this setting?
    2. Is typing modulo explicit `≡_N` the right long-term answer?
    3. Is there prior art in reflective or higher-order process calculi that handles this cleanly?
    4. Does the enriched-Lawvere / OSLF viewpoint suggest the right saturation or quotient directly?
    5. Is the current agreement-fragment theorem a good staging theorem, or a sign that the theorem layer is still framed at the wrong abstraction level?

    ## Prior Art We Would Especially Like Compared

    From the bundled papers:

    - Meredith & Radestock 2005
    - `oslf.pdf`
    - Stay & Meredith 2017 on enriched Lawvere theories
    - Meredith & Stay 2017 name-free combinators
    - Lybech 2022 / 2024 / 2025
    - higher-order psi-calculi typing papers in the same paper subset

    ## What We Are Not Asking For

    - not a code review
    - not scheduler/reduction-limit advice
    - not Rholang feature design
    - not a full Lean proof

    We want the right theorem shape and the right abstract formulation.

    ## Recommended Reading Order

    1. `REVIEW_CONTEXT.md`
    2. `progress.md`
    3. `papers/Meredith_Radestock_2005_A_Reflective_Higher_Order_Calculus.pdf`
    4. `mettapedia-rho-snapshot/Mettapedia/Languages/ProcessCalculi/RhoCalculus/SemanticSubstitution.lean`
    5. `mettapedia-rho-snapshot/Mettapedia/Languages/ProcessCalculi/RhoCalculus/Reduction.lean`
    6. `mettapedia-rho-snapshot/Mettapedia/Languages/ProcessCalculi/RhoCalculus/Soundness.lean`
    7. `mettapedia-rho-snapshot/Mettapedia/OSLF/Framework/BeckChevalleyOSLF.lean`
    8. `cetta-rho-review/src/rhocalc_core.c`
    9. `cetta-rho-review/tests/rhocalc_lean_microcheck.lean`

    ## Deliverable We Want

    A short technical memo addressing:

    1. recommended theorem shape
    2. why it is the right abstraction level
    3. key lemmas or definitions it would require in Lean
    4. relevant prior art
    5. any warning signs in the current agreement-fragment approach
    """
)

write(
    "mettapedia-rho-snapshot/BUILD_INSTRUCTIONS.md",
    """
    # Build Instructions

    This snapshot intentionally omits `.lake/` caches and build artifacts.

    Verified commands on the source machine:

    ```bash
    lake build Mettapedia.Languages.ProcessCalculi.RhoCalculus.SemanticSubstitution
    lake build Mettapedia.Languages.ProcessCalculi.RhoCalculus
    lake build Mettapedia.OSLF.Framework.BeckChevalleyOSLF
    ```

    If your environment does not already have Lean dependencies restored,
    `lake` may need to fetch them using `lake-manifest.json`.
    """
)

write(
    "papers/INDEX.md",
    """
    # Paper Index

    Core references:

    - `Meredith_Radestock_2005_A_Reflective_Higher_Order_Calculus.pdf`
    - `oslf.pdf`
    - `Stay_Meredith_2017_Representing_Operational_Semantics_with_Enriched_Lawvere_Theories.pdf`
    - `Meredith_Stay_2017_Name_Free_Combinators_for_Concurrency.pdf`
    - `Meredith_Pettersson_Stephenson_Stay_Shikama_Denman_2018_Rholang_Specification_Draft_0_2.pdf`

    Typing / encodability / related prior art:

    - `Lybech_2022_Encodability_and_Separation_for_a_Reflective_Higher_Order_Calculus_EPTCS.pdf`
    - `Lybech_2024_The_Reflective_Higher_Order_Calculus_Encodability_Typability_and_Separation.pdf`
    - `Lybech_2025_A_Type_Theoretic_Approach_to_Smart_Contract_Safety.pdf`
    - `Bendixen_Bojesen_Huttel_Lybech_2022_A_Generic_Type_System_for_Higher_Order_Psi_Calculi_EPTCS.pdf`
    - `Huttel_Lybech_Bendixen_Bojesen_2024_A_Generic_Type_System_for_Higher_Order_Psi_Calculi_Information_and_Computation.pdf`

    Broader rho / Rholang context:

    - `Rholang_V1.1.pdf`
    - `Rholang_Cheat_Sheet_2018.pdf`
    - `Meredith_2015_Linear_Types_Can_Change_the_Blockchain.pdf`
    - `Stay_Meredith_2016_Logic_as_a_Distributive_Law.pdf`
    """
)
PY

papers=(
  "Meredith_Radestock_2005_A_Reflective_Higher_Order_Calculus.pdf"
  "Meredith_Pettersson_Stephenson_Stay_Shikama_Denman_2018_Rholang_Specification_Draft_0_2.pdf"
  "Stay_Meredith_2017_Representing_Operational_Semantics_with_Enriched_Lawvere_Theories.pdf"
  "Meredith_Stay_2017_Name_Free_Combinators_for_Concurrency.pdf"
  "Lybech_2022_Encodability_and_Separation_for_a_Reflective_Higher_Order_Calculus_EPTCS.pdf"
  "Lybech_2024_The_Reflective_Higher_Order_Calculus_Encodability_Typability_and_Separation.pdf"
  "Lybech_2025_A_Type_Theoretic_Approach_to_Smart_Contract_Safety.pdf"
  "Bendixen_Bojesen_Huttel_Lybech_2022_A_Generic_Type_System_for_Higher_Order_Psi_Calculi_EPTCS.pdf"
  "Huttel_Lybech_Bendixen_Bojesen_2024_A_Generic_Type_System_for_Higher_Order_Psi_Calculi_Information_and_Computation.pdf"
  "Meredith_2015_Linear_Types_Can_Change_the_Blockchain.pdf"
  "Stay_Meredith_2016_Logic_as_a_Distributive_Law.pdf"
  "Rholang_V1.1.pdf"
  "oslf.pdf"
)

for paper in "${papers[@]}"; do
  cp "$LIT/$paper" "$ROOT/papers/"
done

{
  echo "# Workspace State"
  echo
  echo "Bundle: $NAME"
  echo "Generated: $STAMP"
  echo
  echo "## cetta-hyperpose"
  git -C "$SRC_CETTA" log -1 --oneline 2>/dev/null || true
  git -C "$SRC_CETTA" status --short --branch 2>/dev/null || true
  echo
  echo "## mettapedia source tree"
  echo "Git root: $SRC_ROOT"
  git -C "$SRC_ROOT" log -1 --oneline 2>/dev/null || true
  git -C "$SRC_ROOT" status --short --branch -- \
    lean-projects/mettapedia \
    hyperon/cetta-hyperpose \
    2>/dev/null || true
  echo
  echo "## Bundle Notes"
  echo "- mettapedia snapshot includes the full local source tree under lean-projects/mettapedia, but omits .lake caches and build artifacts for size."
  echo "- local Lean path dependencies are vendored under algorithms/, batteries/, and hyperon/metamath/mm-lean4/."
  echo "- source-machine verification may reuse the existing mettapedia .lake package cache via a temporary symlink; the final archive omits .lake/."
  echo "- cetta snapshot omits build artifacts and target directories."
} > "$ROOT/workspace_state.txt"

(
  cd "$ROOT/mettapedia-rho-snapshot"
  ulimit -v 10485760
  lake build Mettapedia.Languages.ProcessCalculi.RhoCalculus.SemanticSubstitution \
    > "$ROOT/logs/lean_semantic_substitution.log" 2>&1
)

(
  cd "$ROOT/mettapedia-rho-snapshot"
  ulimit -v 10485760
  lake build Mettapedia.Languages.ProcessCalculi.RhoCalculus \
    > "$ROOT/logs/lean_rhocalc.log" 2>&1
)

(
  cd "$ROOT/mettapedia-rho-snapshot"
  ulimit -v 10485760
  lake build Mettapedia.OSLF.Framework.BeckChevalleyOSLF \
    > "$ROOT/logs/lean_beck_chevalley.log" 2>&1
)

(
  cd "$ROOT/cetta-rho-review"
  ulimit -v 10485760
  python3 scripts/rhocalc_lean_microcheck.py \
    "$ROOT/mettapedia-rho-snapshot" \
    tests/rhocalc_lean_microcheck.lean \
    > "$ROOT/logs/cetta_lean_microcheck.log" 2>&1
)

(
  cd "$ROOT/cetta-rho-review"
  ulimit -v 10485760
  METTAPEDIA_ROOT="$ROOT/mettapedia-rho-snapshot" \
    make -j1 BUILD=core test-rhocalc > "$ROOT/logs/cetta_test_rhocalc.log" 2>&1
)

RHOCALC_RESULT="$(tail -n 1 "$ROOT/logs/cetta_test_rhocalc.log")"

{
  echo
  echo "## Build verification"
  echo "test-rhocalc: ${RHOCALC_RESULT}"
  echo "lake build SemanticSubstitution: passed"
  echo "lake build RhoCalculus (umbrella): passed"
  echo "lake build BeckChevalleyOSLF: passed"
  echo "rhocalc Lean microcheck: passed"
} >> "$ROOT/workspace_state.txt"

rm -rf "$ROOT/cetta-rho-review/build" \
       "$ROOT/cetta-rho-review/target" \
       "$ROOT/cetta-rho-review/cetta" \
       "$ROOT/mettapedia-rho-snapshot/.lake" \
       "$ROOT/algorithms/.lake" \
       "$ROOT/batteries/mettail-core/.lake" \
       "$ROOT/batteries/gf-core/.lake" \
       "$ROOT/hyperon/metamath/mm-lean4/.lake"

(cd "$ROOT" && find . -type f | sort > filelist.txt)

(
  cd /home/zar/claude/review_bundles
  zip -rq "${NAME}.zip" "${NAME}/"
  sha256sum "${NAME}.zip" > "${NAME}.zip.sha256"
)

SIZE_BYTES="$(stat -c %s "$ZIP")"
SIZE_HUMAN="$(du -sh "$ZIP" | awk '{print $1}')"
SHA_VALUE="$(cut -d' ' -f1 "$HASH")"

echo "Bundle ready:"
echo "  path: $ZIP"
echo "  sha256: $SHA_VALUE"
echo "  size-bytes: $SIZE_BYTES"
echo "  size-human: $SIZE_HUMAN"
