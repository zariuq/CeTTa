; Computed Space reads belong to commit validation and parked wakeups.
!(import! &self interaction)
!(bind! &cd-tick (new-space))

; An unchanged complete read may publish its result.
!(bind! &cd-good (new-space))
!(bind! &cd-data (new-space))
!(add-atom &cd-good pending)
!(add-atom &cd-data (item 1))
!(assertEqual (transform &cd-good
   (, (take pending) (let $xs (collapse (match &cd-data (item $x) $x))))
   (add-atom &cd-good (observed $xs))) ())
!(assertEqual (collapse (match &cd-good (observed $xs) $xs)) ((1)))

; A later insertion invalidates that complete read, even though its old
; witness remains present. The computed write itself remains observable.
!(bind! &cd-insert (new-space))
!(bind! &cd-insert-data (new-space))
!(add-atom &cd-insert pending)
!(add-atom &cd-insert-data (item 1))
!(assertEqual (collapse (transform &cd-insert
   (, (take pending)
      (let $xs (collapse (match &cd-insert-data (item $x) $x)))
      (let $_ (add-atom &cd-insert-data (item 2))))
   (add-atom &cd-insert done))) ())
!(assertEqual (collapse (get-atoms &cd-insert)) (pending))
!(assertEqual (sort-atom (collapse (get-atoms &cd-insert-data))) ((item 1) (item 2)))

; Removal and restoration of the same payload do not restore the old token.
!(bind! &cd-aba (new-space))
!(bind! &cd-aba-data (new-space))
!(add-atom &cd-aba pending)
!(add-atom &cd-aba-data marker)
!(assertEqual (collapse (transform &cd-aba
   (, (take pending)
      (let $xs (collapse (match &cd-aba-data marker marker)))
      (let $_ (remove-atom &cd-aba-data marker))
      (let $_ (add-atom &cd-aba-data marker)))
   (add-atom &cd-aba done))) ())
!(assertEqual (collapse (get-atoms &cd-aba)) (pending))
!(assertEqual (collapse (get-atoms &cd-aba-data)) (marker))

; Empty results are observations too. Inserting the first row invalidates
; the absence obtained by the earlier computed query.
!(bind! &cd-empty (new-space))
!(bind! &cd-empty-data (new-space))
!(add-atom &cd-empty pending)
!(assertEqual (collapse (transform &cd-empty
   (, (take pending)
      (let () (collapse (match &cd-empty-data marker marker)))
      (let $_ (add-atom &cd-empty-data marker)))
   (add-atom &cd-empty done))) ())
!(assertEqual (collapse (get-atoms &cd-empty)) (pending))
!(assertEqual (collapse (get-atoms &cd-empty-data)) (marker))

; A write to an unconsulted space does not invalidate the captured query.
!(bind! &cd-other (new-space))
!(bind! &cd-other-data (new-space))
!(bind! &cd-unrelated (new-space))
!(add-atom &cd-other pending)
!(assertEqual (transform &cd-other
   (, (take pending)
      (let () (collapse (match &cd-other-data marker marker)))
      (let $_ (add-atom &cd-unrelated marker)))
   (add-atom &cd-other done)) ())
!(assertEqual (collapse (get-atoms &cd-other)) (done))
!(assertEqual (collapse (get-atoms &cd-unrelated)) (marker))

; A computed read with no results still registers its dependency. An arrival
; wakes the parked interaction at the next agenda drive, without a source
; pattern explicitly mentioning that space.
!(bind! &cd-await (new-space))
!(bind! &cd-arrival (new-space))
!(add-atom &cd-await pending)
!(await (transform &cd-await
   (, (take pending) (let ready (match &cd-arrival ready ready)))
   (add-atom &cd-await done)))
!(assertEqual (collapse (get-atoms &cd-await)) (pending))
!(add-atom &cd-arrival ready)
!(transform &cd-tick () ())
!(assertEqual (collapse (get-atoms &cd-await)) (done))

; Removing a blocker enables a computed negative query and wakes its waiter.
!(bind! &cd-removal (new-space))
!(bind! &cd-blocker (new-space))
!(add-atom &cd-removal pending)
!(add-atom &cd-blocker blocked)
!(await (transform &cd-removal
   (, (take pending) (let () (collapse (match &cd-blocker blocked blocked))))
   (add-atom &cd-removal done)))
!(assertEqual (collapse (get-atoms &cd-removal)) (pending))
!(remove-atom &cd-blocker blocked)
!(transform &cd-tick () ())
!(assertEqual (collapse (get-atoms &cd-removal)) (done))

; Computed premises use Prime's ordinary value boundary. A stored record
; can be inspected through a constructor argument without exposing its held
; carrier to the checker or executing code contained in the record.
!(bind! &cv-work (new-space))
!(bind! &cv-effects (new-space))
(= (cv-record (Request (Envelope $value))) (Some $value))
(= (cv-inspect (Request $record))
   (case $record (((Envelope $value) (Some 7)) ($_ None))))
(= (cv-effect) (let $_ (add-atom &cv-effects executed) 42))
!(add-atom &cv-work (stored (Envelope 7)))
!(assertEqual (transform &cv-work
   (, (take (stored $record)) (let (Some $value) (cv-record (Request $record))))
   (add-atom &cv-work (checked $value))) ())
!(assertEqual (collapse (get-atoms &cv-work)) ((checked 7)))
!(add-atom &cv-work (code (Envelope (cv-effect))))
!(assertEqual (transform &cv-work
   (, (take (code $record)) (let (Some 7) (cv-inspect (Request $record))))
   (add-atom &cv-work inspected)) ())
!(assertEqual (collapse (get-atoms &cv-effects)) ())

; Opening a stored program explicitly runs it once.
!(add-atom &cv-work (run (cv-effect)))
!(assertEqual (transform &cv-work
   (, (take (run $code)) (let 42 (eval $code)))
   (add-atom &cv-work complete)) ())
!(assertEqual (collapse (get-atoms &cv-effects)) (executed))

; Source currency observations report the Space they inspected. A later
; computed write invalidates the publication while preserving that write.
!(import! &self work)
!(bind! &cv-epoch-data (new-space))
!(add-atom &cv-work currency-pending)
!(assertEqual (collapse (transform &cv-work
   (, (take currency-pending) (let $epoch (work:source-epoch &cv-epoch-data))
      (let $_ (add-atom &cv-epoch-data changed)))
   (add-atom &cv-work currency-forbidden))) ())
!(assertEqual (collapse (match &cv-work currency-pending currency-pending)) (currency-pending))
!(assertEqual (collapse (get-atoms &cv-epoch-data)) (changed))
!(assertEqual (collapse (match &cv-work currency-forbidden currency-forbidden)) ())

; A fresh epoch and its current-status observation may publish.
!(assertEqual (transform &cv-work
   (, (take currency-pending) (let $epoch (work:source-epoch &cv-epoch-data))
      (let Work:Current (work:source-status &cv-epoch-data $epoch)))
   (add-atom &cv-work currency-done)) ())
!(assertEqual (collapse (match &cv-work currency-done currency-done)) (currency-done))
!(assertEqual (collapse (match &cv-work currency-pending currency-pending)) ())
