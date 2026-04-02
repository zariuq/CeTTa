; Prototype umbrella for CeTTa list design experiments.
;
; Current split:
; - list_expr: expression-backed sequence helpers for the efficient default lane
; - list_cons: explicit cons-list helpers for inductive/logical structure
;
; Working hypothesis:
; - expressions should remain the default fast list-like runtime container
; - explicit cons-lists should stay a library-level semantic encoding

!(import! &self list_expr)
!(import! &self list_cons)
