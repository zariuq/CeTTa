; Strict-core rho surface.
; Canonical terms use the rho: namespace. Dotted rho. spelling is reader
; sugar for the same symbols.
;
; Importing rho only provides constructor-level surface vocabulary for building
; strict-core rho terms in MeTTa.
;
; It must not expose evaluator or relation helpers such as:
;   rho:step
;   rho:frontier
;   rho:reduce
;   rho:eval
;
; Program execution belongs to:
;   cetta --lang rhocalc [--rho-reduction-limit N] [--rho-scheduler NAME] <file>
