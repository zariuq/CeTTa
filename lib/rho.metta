; Strict-core rho surface.
; Canonical terms use the rho: namespace. Dotted rho. spelling is reader
; sugar for the same symbols.
;
; Importing rho only provides constructor-level surface vocabulary for building
; strict-core rho terms in MeTTa.
;
; It does not validate constructor arity or well-formedness at import time:
; malformed rho constructor shapes remain inert surface syntax here. Structural
; validation belongs to rhocalc execution and the lts:rho / lts:rho:cost
; frontiers, not to lib/rho. The dotted rho. reader sugar does not bypass this:
; malformed dotted constructor forms remain the same inert syntax too.
;
; It must not expose evaluator or relation helpers such as:
;   rho:step
;   rho:frontier
;   rho:reduce
;   rho:eval
; The dotted rho. reader sugar does not bypass this boundary either: dotted
; helper names remain plain syntax unless some other library defines them.
;
; Program execution belongs to:
;   cetta --lang rhocalc [--rho-reduction-limit N] [--rho-scheduler NAME] <file>
