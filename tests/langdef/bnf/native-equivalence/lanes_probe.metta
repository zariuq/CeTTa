!(import! &self ../../../../lib/lib_bnf.metta)
(= (doc) (bnf-v1:document (bnf-v1:entries-cons (bnf-v1:rule (bnf-v1:text-cons 115 (bnf-v1:text-nil)) (bnf-v1:expression (bnf-v1:alternatives-cons (bnf-v1:alternative (bnf-v1:elements-cons (bnf-v1:literal (bnf-v1:text-cons 120 (bnf-v1:text-nil)) (bnf-v1:source-span 0 3)) (bnf-v1:elements-nil)) (bnf-v1:source-span 0 3)) (bnf-v1:alternatives-nil)) (bnf-v1:source-span 0 4)) (bnf-v1:source-span 0 12)) (bnf-v1:entries-nil)) (bnf-v1:source-span 0 12)))
(= (receipt) (EBNF:LoweredGrammar (doc) (bnf:default-authority "s") (doc) (ebnf-v1:origins-nil)))
!(println! (Lane (bnf:project-ebnf-trees (receipt) ((CstRuleV1 "s#" 0 1)))))
