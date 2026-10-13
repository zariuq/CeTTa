!(import! &self "../lib/lib_module.metta")

; A declared binder, not a host-language lambda. The explicit projection
; interprets Name tokens as references in their category's scoped namespace.
!(let*
  (($reader (langdef:load-baked-parser "module-core-reader"))
   ($module (module:read-file $reader "bindings" "tests/module_values/BindingValues.module"))
   ($reader-closed (langdef:close-parser $reader))
   ($theory (module:assemble $module "BindingWeaken" LNil LNil OriginViewV1 "example:bindings"))
   ($language (module:language $theory "NamedBindings" "Expr" (SyntaxLexicalPolicyV1 bnfc-documented)))
   ($core (module:language-core $language))
   ($syntax (module:language-syntax $language))
   ($authority (langdef:load-language-value $core))
   ($parser (langdef:compile-parser $syntax auto))
   ($parsed (langdef:parser-parse-text $parser "lam x.lam y.ref(x)"))
   ($projected (langdef:project-parser-pattern-result $authority "Expr"
                 (LangDef:NamedTokensV1 (LCons "Name" LNil)) $parsed))
   ($term (langdef:projected-term $projected 0))
   ($admitted (langdef:admit-pattern-data $authority "Expr" $term))
   ($rewritten (langdef:rewrite-pattern-at-data $authority 4 100000 (LangDef:RelationRows) $term))
   ($reduct (langdef:contextual-result-term $rewritten 0))
   ($reduct-admitted (langdef:admit-pattern-data $authority "Expr" $reduct))
   ($unbound-parse (langdef:parser-parse-text $parser "lam x.ref(y)"))
   ($unbound (langdef:project-parser-pattern-result $authority "Expr"
               (LangDef:NamedTokensV1 (LCons "Name" LNil)) $unbound-parse))
   ($parser-closed (langdef:close-parser $parser))
   ($authority-closed (langdef:close-language $authority)))
  (function (return (ModuleBindingExampleV1 $term $admitted $unbound $projected $rewritten $reduct-admitted))))
