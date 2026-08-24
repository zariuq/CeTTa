; Generated from the identity-pinned curried chaining source.
(gslt-presentation-v1 CurriedChainingRulePackageV1
  (signature
    (operator → 2))
  (equations)
  (rewrites
    (rule curried-chaining-a (head (source-block curried-chaining (rm-block a curried-chainer.metta:a (rm-proof-atom a) rm-nil A))) (body))
    (rule curried-chaining-ab (head (source-block curried-chaining (rm-block ab curried-chainer.metta:ab (rm-proof-atom ab) rm-nil (→ A B)))) (body))
    (rule curried-chaining-bc (head (source-block curried-chaining (rm-block bc curried-chainer.metta:bc (rm-proof-atom bc) rm-nil (→ B C)))) (body))
    (rule curried-chaining-modus-ponens (head (source-block curried-chaining (rm-block modus-ponens curried-chainer.metta:ModusPonens (rm-proof-app ModusPonens (rm-cons ?implicationProof (rm-cons ?premiseProof rm-nil))) (rm-cons (rm-premise ?implicationProof (→ ?p ?q)) (rm-cons (rm-premise ?premiseProof ?p) rm-nil)) ?q))) (body))
    (rule curried-chaining-r0 (head (guest-revision curried-chaining r0 (artifact-link (artifact-link (artifact-link (artifact-link (artifact-empty curried-chaining) (block-ref curried-chaining a)) (block-ref curried-chaining ab)) (block-ref curried-chaining bc)) (block-ref curried-chaining modus-ponens)))) (body))))
