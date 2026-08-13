; A relation call retains the clause occurrences visible when the call begins.
; Its first clause removes the second occurrence and adds a third; the active
; call still completes the removed clause and cannot see the new one.
(= (logical-update-call)
   (let* (($removed
             (remove-atom &self
                (= (logical-update-call) second)))
           ($added
             (add-atom &self
                (= (logical-update-call) third))))
      first))
(= (logical-update-call) second)

; Declaration order and duplicate occurrences are observable.
(= (ordered-occurrences) first-order)
(= (ordered-occurrences) duplicate)
(= (ordered-occurrences) duplicate)
(= (ordered-occurrences) last-order)

!(collapse (logical-update-call))
; A later call sees the completed mutation, while its own newly asserted
; occurrence again remains invisible until another call begins.
!(collapse (logical-update-call))
!(collapse (ordered-occurrences))
