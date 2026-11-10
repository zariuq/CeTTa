; CeTTa's PeTTa transform operation.  The full authored implementation is kept
; separately as lib_transform_swi for SWI-PeTTa and reference checks. This
; module exposes the native operation with editable public equations.
;
; (transform SPACE SOURCES TEMPLATE) returns the instances of each firing.
; (transform SPACE SOURCES TEMPLATE DESTINATION) routes them at the commit.
; A space receives them and the answer is their count; a function is applied
; to each; tf:caller evaluates each instance as match evaluates its template.
;
; Sources are one conjunct, a conjunction (, c1 ... cn), or () for one empty
; row.  p reads, (take [S] p) consumes, (absent [S] p) requires no match,
; (unique c) requires one, (collect c C) collects every match, (let P E)
; evaluates before the commit, (all c) fires every extension, and (once c)
; commits the first extension that holds.  A template (, t1 ... tn) gives n
; instances per row by its own syntax.
;
; Proposals are made once against the store at entry.  Each becomes one
; branch; a branch fires only when continued, against the store then current.
; Reads alone answer as match.  Takes validate occurrence multiplicity before
; the commit, and nothing runs between validation and the commit.  Proposals,
; firings and collect keep occurrence order; collected reads-only answers are
; a bag where the consumer takes every answer.  PeTTa's remove-atom removes all
; copies, so the commit restores those not taken.

(: transform (-> Atom Atom Atom %Undefined%))
(: transform (-> Atom Atom Atom Atom %Undefined%))
(= (transform $space $sources $template) (tf:run $space $sources $template tf:caller))
(= (transform $space $sources $template $destination)
   (tf:run $space $sources $template $destination))

; Native relation availability, not authored implementations.  The host
; handles each head before relation search; an unhandled call has no fallback.
(= (tf:run $a $b $c $d) (empty))
(= (tf:split $a $b) (empty))
(= (tf:propose $a $b $c) (empty))
(= (tf:inst $a $b) (empty))
(= (tf:fire $a $b $c) (empty))
(= (tf:fire-into $a $b $c $d) (empty))
