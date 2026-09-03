#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 <Mettapedia-root> [--write]" >&2
    exit 2
fi
if [ "$#" -eq 2 ] && [ "$2" != "--write" ]; then
    echo "unknown option: $2" >&2
    exit 2
fi

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mettapedia_root=$1
generated_dir="$project_root/runtime/bootstrap"
checked_data="$project_root/langdef/tptp/generated/official_ground_resolution_word_canary_v1.metta"

mkdir -p "$generated_dir"
candidate=$(mktemp "$generated_dir/tptp-ground-resolution-word-canary.XXXXXX")
trap 'rm -f "$candidate"' EXIT INT TERM

if ! (
    cd "$mettapedia_root/lean/mettapedia"
    lake env lean --stdin <<'LEAN'
import Mettapedia.GSLT.LanguageDef.TptpOfficialGroundResolutionStatusIndexedWordMachine

namespace TptpGroundResolutionWordCanaryExport

open Mettapedia.OSLF.MeTTaIL.Syntax
open Mettapedia.OSLF.MeTTaIL.Match
open Mettapedia.OSLF.MeTTaIL.Engine
open Mettapedia.OSLF.MeTTaIL.ReflectiveCanonical
open Mettapedia.OSLF.MeTTaIL.ReflectiveSubstitution
open Mettapedia.GSLT.LanguageDef.TptpOfficialGroundResolutionStatusIndexedWordMachine
open Mettapedia.GSLT.LanguageDef.DerivationWordMachineLanguageDef
open Mettapedia.GSLT.LanguageDef.DerivationWordMachineRelationEnv

abbrev Row := String × List Pattern

structure Branch where
  bindings : Bindings
  rows : List Row

private def stepPremises (environment : RelationEnv) (definition : LanguageDef) :
    List Premise → Branch → List Branch
  | [], branch => [branch]
  | premise :: premises, branch =>
      let next : List Branch :=
        match premise with
        | .relationQuery relation arguments =>
            let applied := arguments.map (applyBindings branch.bindings)
            let tuples :=
              builtinRelationTuples definition relation applied ++
                environment.tuples relation applied
            tuples.flatMap fun tuple =>
              (matchRelationArgs branch.bindings arguments tuple).filterMap
                fun extension =>
                  match mergeBindings branch.bindings extension with
                  | none => none
                  | some bindings =>
                      some {
                        bindings
                        rows := branch.rows ++ [(relation, tuple)]
                      }
        | _ =>
            (premiseStepWithEnv environment definition branch.bindings premise).map
              fun bindings => { bindings, rows := branch.rows }
      next.flatMap (stepPremises environment definition premises)

def stepLogged (term : Pattern) : List (Pattern × List Row) :=
  language.rewrites.flatMap fun rule =>
    (matchPatternForRule language rule term).flatMap fun bindings =>
      (stepPremises (relationEnv Canary.wordHost) language rule.premises
        { bindings, rows := [] }).map fun final =>
          (applyBindingsForRule language rule final.bindings, final.rows)

def runLogged : Nat → Pattern → List (Pattern × List Row)
  | 0, term => [(term, [])]
  | fuel + 1, term =>
      (stepLogged term).flatMap fun next =>
        (runLogged fuel next.1).map fun final =>
          (final.1, next.2 ++ final.2)

private def renderRaw? : Pattern → Option String
  | .apply label [] =>
      if label.toNat?.isSome then some label else some s!"({label})"
  | .apply label arguments => do
      let rendered ← arguments.mapM renderRaw?
      some s!"({label} {String.intercalate " " rendered})"
  | _ => none

mutual
  private def renderPatternList? : List Pattern → Option String
    | [] => some "LNil"
    | pattern :: patterns => do
        let head ← renderPatternWire? pattern
        let tail ← renderPatternList? patterns
        some s!"(LCons {head} {tail})"

  private def renderPatternWire? : Pattern → Option String
    | .bvar index => some s!"(BVar {index})"
    | .fvar name => some s!"(FVar {reprStr name})"
    | .apply label arguments => do
        let rendered ← renderPatternList? arguments
        some s!"(PApp {reprStr label} {rendered})"
    | _ => none
end

private def renderRow? (entry : Nat × Row) : Option String := do
  let (index, relation, tuple) := (entry.1, entry.2.1, entry.2.2)
  let renderedTuple ← tuple.mapM renderPatternWire?
  let suffix :=
    if renderedTuple.isEmpty then ""
    else " " ++ String.intercalate " " renderedTuple
  some s!"    (LangDef:RelationRow {reprStr relation} {920000 + index}{suffix})"

private def renderArtifact? : Option String := do
  let (target, rows) ←
    match runLogged 7 Canary.wordStart with
    | [result] => some result
    | _ => none
  match target with
  | .apply "dwm:halted"
      [.apply "dcm:outcome-verified" [_, _, _], _] => pure ()
  | _ => none
  if rows.length != 27 || rows.eraseDups.length != 27 then none else pure ()
  let start ← renderRaw? Canary.wordStart
  let expected ← renderRaw? target
  let renderedRows ← (List.range rows.length |>.zip rows).mapM renderRow?
  some <| String.intercalate "\n" [
    "; Generated from the theorem-checked status-indexed official derivation.",
    "; This is data only: the generic word machine contains no calculus policy.",
    "",
    "(= (tptp-ground-word-canary:start)",
    s!"   {start})",
    "",
    "(= (tptp-ground-word-canary:expected)",
    s!"   {expected})",
    "",
    "(= (tptp-ground-word-canary:relation-rows)",
    "   (LangDef:RelationRows",
    String.intercalate "\n" renderedRows,
    "   ))",
    "",
    "(= (tptp-ground-word-canary:step-count) 7)",
    "(= (tptp-ground-word-canary:relation-row-count) 27)",
    ""
  ]

#eval do
  match renderArtifact? with
  | some artifact => IO.print artifact
  | none => throw <| IO.userError "ground-resolution word canary export failed closed"

end TptpGroundResolutionWordCanaryExport
LEAN
) >"$candidate"; then
    cat "$candidate" >&2
    echo "official ground-resolution word canary export failed" >&2
    exit 1
fi

if [ "$#" -eq 2 ]; then
    mkdir -p "$(dirname -- "$checked_data")"
    cp "$candidate" "$checked_data"
    echo "WROTE: official ground-resolution word canary data"
    exit 0
fi

if ! cmp -s "$candidate" "$checked_data"; then
    echo "generated official ground-resolution word canary differs from the checked artifact" >&2
    exit 1
fi

echo "PASS: official ground-resolution word canary is byte-identical to its Lean projection"
