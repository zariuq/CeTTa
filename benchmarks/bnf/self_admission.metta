; Load the generated admission program, lib_bnf and the self authority first.
; Reading and admission are both included in this workload.
(= (bench:self-observe
     (BNF:SemanticGrammarAdmitted $_binding $_source $_document $_authority
       (BNFSemanticValidationAcceptedV1 $_start $_definitions $_lexicals $_analysis)))
   BnfBenchSelfAccepted)
!(println! (bench:self-observe
   (bnf:validate (bnf:read-file "langdef/bnf/plain_bnf_self_v1.bnf")
     (bnf:self-authority-v1))))
