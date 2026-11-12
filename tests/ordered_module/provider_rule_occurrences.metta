; This is the existing checked rule view, after the textual module boundary.
(ModuleRuleOccurrenceV1
  (SyntaxOriginV1 "ordered-provider-extension" "declaration:0")
  (RewriteRule "ProviderEcho"
    (LCons (TypeBinding "value" (TBase "Nat")) LNil)
    (LCons (RelationQuery "allowed" (LCons (FVar "value") LNil)) LNil)
    (PApp "NatAdd" (LCons (PApp "NatZ" LNil) (LCons (FVar "value") LNil)))
    (FVar "value")))
