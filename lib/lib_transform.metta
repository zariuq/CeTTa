; CeTTa's HE transform operation.  The full authored HE implementation is kept
; as a test-only reference; this module exposes the native operation while
; leaving its public equations editable.
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
; a bag where the consumer takes every answer.  HE's remove-atom removes one
; copy, so a firing removes exactly the copies it takes.

(: transform (-> Atom Atom Atom %Undefined%))
(: transform (-> Atom Atom Atom Atom %Undefined%))
(= (transform $space $sources $template) (tf:run $space $sources $template tf:caller))
(= (transform $space $sources $template $destination)
   (tf:run $space $sources $template $destination))

; Internal carriers hold syntax. In particular an empty guide is data,
; and a source-level let must not run while its carrier is being inspected.
(: tf:cand (-> Atom TfData))
(: tf:cs (-> Atom TfData))
(: tf:fired (-> Atom TfData))
(: tf:run (-> Atom Atom Atom Atom %Undefined%))
(: tf:split (-> Atom Atom %Undefined%))
(: tf:cands (-> Atom Atom Atom %Undefined%))
(: tf:pin (-> Atom Atom %Undefined%))
(: tf:fire (-> Atom Atom Atom %Undefined%))
(: tf:fire-into (-> Atom Atom Atom Atom %Undefined%))

; Native relation availability, not authored implementations.  The host
; handles each head before relation search; an unhandled call has no fallback.
(= (tf:run $a $b $c $d) (empty))
(= (tf:split $a $b) (empty))
(= (tf:cands $a $b $c) (empty))
(= (tf:pin $a $b) (empty))
(= (tf:fire $a $b $c) (empty))
(= (tf:fire-into $a $b $c $d) (empty))
