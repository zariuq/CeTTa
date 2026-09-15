; The test-only observer meters only typed-lifetime:probe. Preparation, the
; subsequent explicit recovery close, and the following query are unmetered.
!(let (BNF:ReadValue (BNF:GrammarDocument $binding $source $document))
      (bnf:read-text "<s> ::= \"x\"\n")
   (let $authority (bnf:default-authority "s")
     (let $language (langdef:load-language "langdef/bnf/plain_bnf_grammar_v1.metta")
       (add-atom &self (TypedLifetimeInputV1 $language $document $authority)))))

(= (typed-lifetime:probe)
   (match &self (TypedLifetimeInputV1 $language $document $authority)
     (bnf-typed-discovery:validate-owned-document $language $document $authority)))
!(typed-lifetime:probe)
!(match &self (TypedLifetimeInputV1 $language $document $authority)
   (TypedLifetimeRecoveryCloseV1 (langdef:close-language $language)))
!(TypedLifetimeFollowingQueryV1 (+ 20 22))
