; Selecting an identity as a completed expression can interpret Boolean-like
; values. Keep it inside a data constructor until its consumer binds it.
!(import! &self "atom_data.metta")
(: kbg:data-at (-> Atom Number %Undefined%))
(= (kbg:data-at $items $position)
  (if (and (kb:integer? $position) (>= $position 0))
    (case (atom-data:item $items $position)
      (((AtomDataV1 $value) (KgDataV1 $value)) ($failure (KbRefused graph-field))))
    (KbRefused graph-field)))
