import Mettapedia.OSLF.MeTTaIL.Syntax

/-!
Emit the parser-facing abstract-syntax projection of the authored rho
`LanguageDef`.  The output intentionally contains only sorts, constructor
result sorts, argument types, and binder structure.  Concrete notation and
the runtime representation are separate presentations.
-/

namespace CeTTa.GSLT2Parse.ExportRhoAbstractSyntaxV1

open Mettapedia.OSLF.MeTTaIL.Syntax

private def renderCarrier : CarrierKind → String
  | .ast => "ast"
  | .tokenLabel => "token-label"
  | .tokenRaw => "token-raw"
  | .tokenProof => "token-proof"
  | .tokenPath => "token-path"
  | .builtinInt => "builtin-int"
  | .builtinString => "builtin-string"
  | .builtinBool => "builtin-bool"

private def renderCollection : CollType → String
  | .vec => "vec"
  | .hashBag => "hash-bag"
  | .hashSet => "hash-set"

private def renderType : TypeExpr → String
  | .base name => name
  | .arrow domain codomain =>
      s!"(rho-arrow {renderType domain} {renderType codomain})"
  | .multiBinder body => s!"(rho-multi-binder {renderType body})"
  | .collection kind element =>
      s!"(rho-collection {renderCollection kind} {renderType element})"

private def renderParam : TermParam → String
  | .simple _ type => s!"(rho-simple {renderType type})"
  | .abstractionNamed _ _ type =>
      s!"(rho-abstraction {renderType type})"
  | .multiAbstractionNamed _ _ type =>
      s!"(rho-multi-abstraction {renderType type})"

private def renderParams : List TermParam → String
  | [] => "rho-no-args"
  | param :: rest =>
      s!"(rho-args {renderParam param} {renderParams rest})"

private def renderSortRule (declaration : TypeDecl) : String :=
  s!"    (rule rho-sort-{declaration.name}\n" ++
  s!"      (head (rho-sort {declaration.name} " ++
  s!"{renderCarrier declaration.carrier}))\n" ++
  "      (body))"

private def renderConstructorRule (rule : GrammarRule) : String :=
  s!"    (rule rho-constructor-{rule.label}\n" ++
  s!"      (head (rho-constructor {rule.label} {rule.category} " ++
  s!"{renderParams rule.params}))\n" ++
  "      (body))"

def render : String :=
  let sortRules := rhoCalc.types.map renderSortRule
  let constructorRules := rhoCalc.terms.map renderConstructorRule
  String.intercalate "\n"
    ([ "; Generated from the authored Mettapedia rhoCalc LanguageDef."
     , "; Concrete .rho/.mrho syntax and the CeTTa runtime ABI are separate data."
     , "(gslt-presentation-v1 RhoAbstractSyntaxV1"
     , "  (signature"
     , "    (operator rho-language 1)"
     , "    (operator rho-sort 2)"
     , "    (operator rho-constructor 3)"
     , "    (operator rho-args 2)"
     , "    (operator rho-simple 1)"
     , "    (operator rho-abstraction 1)"
     , "    (operator rho-multi-abstraction 1)"
     , "    (operator rho-arrow 2)"
     , "    (operator rho-multi-binder 1)"
     , "    (operator rho-collection 2))"
     , "  (equations)"
     , "  (rewrites"
     , s!"    (rule rho-language-{rhoCalc.name}"
     , s!"      (head (rho-language {rhoCalc.name}))"
     , "      (body))"
     ] ++ sortRules ++ constructorRules ++ ["  ))", ""])

end CeTTa.GSLT2Parse.ExportRhoAbstractSyntaxV1

def main : IO Unit :=
  IO.print CeTTa.GSLT2Parse.ExportRhoAbstractSyntaxV1.render
