; HE/Prime tuple error propagation. This is not a PeTTa oracle fixture:
; SWI-PeTTa retains literal Error and Empty inside data constructors.
!(collapse (LCons (Error bad reason) LNil))
!(collapse (LCons Empty LNil))
