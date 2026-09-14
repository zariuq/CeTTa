; Unlike the owned-entry fixture, this caller never receives the language
; handle allocated inside bnf:validate-document. The observer inspects the
; context before and after the evaluation and after final context teardown.
!(let (BNF:ReadValue (BNF:GrammarDocument $binding $source $document))
      (bnf:read-text "<s> ::= \"x\"\n")
   (let $authority (bnf:default-authority "s")
     (add-atom &self (TypedLifetimeDocumentV1 $document $authority))))

(= (typed-lifetime:probe)
   (match &self (TypedLifetimeDocumentV1 $document $authority)
     (bnf:validate-document $document $authority)))
!(typed-lifetime:probe)
!(TypedLifetimeFollowingQueryV1 (+ 20 22))
