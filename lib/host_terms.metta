; Host relations on terms, for any language's rules (a .module theory compiled
; by the direct compiler with --source lib/host_terms.metta):
;   (head-apart A B)  A and B are expressions headed by different symbols
; A rule decides a cover form by its node's head with it, where the rule
; language has no negation: "head-apart"(e, "Paren"()) holds of every node e
; that is not a Paren node.
(gslt-presentation-v1 HostTermsV1
  (signature
    (operator head-apart 2)
    (operator gslt:head-apart 1)
    (operator car-atom 1)
    (operator oslf-external-relation-decl-v1 2)
    (operator oslf-external-relation-modes-v1 2)
    (operator metta-equation 2)
    (operator if 3)
    (operator == 2)
    (operator empty 0)
    (operator quote 1))
  (equations)
  (rewrites
    (rule head-apart-capability (head (oslf-external-relation-decl-v1 head-apart 2)) (body))
    (rule head-apart-modes (head (oslf-external-relation-modes-v1 head-apart "11")) (body))
    (rule head-apart-petta
      (head (metta-equation (gslt:head-apart (head-apart ?a ?b))
                            (if (== (car-atom ?a) (car-atom ?b)) (empty) (quote (head-apart ?a ?b)))))
      (body))))
