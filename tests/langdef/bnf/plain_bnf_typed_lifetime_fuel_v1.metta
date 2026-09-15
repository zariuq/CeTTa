; Retained typed-discovery candidate required. Under --count-only this is an
; actual evaluation-budget exhaustion probe: the CLI reports incompleteness
; and stops the document before the later recovery query. Without count-only,
; the compatibility evaluator uses this setting as depth fuel instead.
!(let (BNF:ReadValue (BNF:GrammarDocument $binding $source $document))
      (bnf:read-text "<s> ::= \"x\"\n")
   (let $authority (bnf:default-authority "s")
     (let $language (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
       (add-atom &self
         (TypedLifetimeInputV1 $language $document $authority)))))

!(pragma! max-stack-depth 640)
!(match &self (TypedLifetimeInputV1 $language $document $authority)
   (bnf-typed-discovery:validate-owned-document $language $document $authority))
!(pragma! max-stack-depth 0)
!(match &self (TypedLifetimeInputV1 $language $document $authority)
   (TypedLifetimeRecoveryCloseV1 (langdef:close-language $language)))
!(TypedLifetimeFollowingQueryV1 (+ 20 22))
