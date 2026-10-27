; Naming is not authority.  A region's space is made fresh and reached only
; through its handles: neither a name nor a plain space operation gives a
; right over it.  A plain space keeps every right, by its name.
!(import! &self interaction)

; a plain space: every right, by its name, inside and outside the kernel
!(bind! &notes (new-space))
!(rights-of &notes)
!(profile-of &notes)
!(transform &notes () (add-atom &notes (note 1)))
!(add-atom &notes (note 2))
!(sort-atom (collapse (transform &notes (every (take (note $n))) (took $n))))

; no operation turns a space that others can name into a region: it stays
; plain, and its name never carries a holder's rights
!(bind! &verdicts (new-space))
!(is-handle (__cetta_lib_interaction_region &verdicts board))
!(profile-of &verdicts)
!(rights-of &verdicts)

; a region's handle is not a space: plain operations do not reach its store
!(let $b (new-region board) (is-handle $b))
!(let $b (new-region board) (add-atom $b (verdict even 7)))
!(let $b (new-region board) (get-atoms $b))
!(let $b (new-region board) (remove-atom $b (verdict even 7)))

; through its handles the board's profile holds: the holder makes, readers peek
!(let* (($b (new-region board)) ($r (facet $b (peek)))
        ($_ (transform $b () (add-atom $b (verdict even 4)))))
   (transform $r (read (verdict even $n)) (saw $n)))
!(let* (($b (new-region board)) ($r (facet $b (peek))))
   (transform $r () (add-atom $r (verdict even 7))))
!(let* (($b (new-region board)) ($r (facet $b (peek)))
        ($_ (transform $b () (add-atom $b (verdict even 4)))))
   (transform $r (take (verdict even $n)) (removed $n)))
; the printed form of a handle names nothing
!(let $b (new-region board) (rights-of (parse (repr $b))))
