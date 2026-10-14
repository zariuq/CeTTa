; Matching an annotated statement in and-then's first operand types the
; fields it binds for the second operand.
(: Proof Type)
(: Claim Type)
(: Strength Type)
(: Annotated Type)
(: : (-[det]-> Proof Claim Strength Annotated))
(: proof-a Proof)
(: claim-a Claim)
(: strength-a Strength)

(: claim-ok? (-[det]-> Claim Bool))
(= (claim-ok? $claim) (== $claim claim-a))

(: annotated-ok? (-[det]-> Annotated Bool))
(= (annotated-ok? $annotated)
   (and-then (= $annotated (: $_proof $claim $_strength))
             (claim-ok? $claim)))

!(test (annotated-ok? (: proof-a claim-a strength-a)) true)
