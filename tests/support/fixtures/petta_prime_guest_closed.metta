; Closed source contracts exercised by the explicit dialect adapter.
(= (guest:bump $value) (+ $value 1))
(: guest:raw (-> Atom %Undefined%))
(= (guest:raw $value) (quote $value))
(= (guest:constant) (quote (tag 3)))

!(GuestControl empty (== (collapse (empty)) ()))
!(GuestControl conditional (== (collapse (if False kept)) ()))
!(GuestControl committed
   (== (collapse (case (quote (tag 1))
        (((tag $value) (empty)) ($fallback fallback)))) ()))
!(GuestControl inert-quotation (== (quote (guest:bump 1)) (quote (guest:bump 1))))
!(GuestControl raw-formal (== (guest:raw (guest:bump 1)) (quote (guest:bump 1))))
!(GuestControl captured-value
   (let $value (guest:bump 1) (== (guest:raw $value) 2)))
!(GuestControl passive-fields (== (ordinary-data (guest:bump 1)) (quote (ordinary-data 2))))
!(GuestControl compound-head
   (== ((tag 1) (guest:bump 1)) (quote ((tag 1) 2))))
!(GuestControl sequence-view
   (case (quote (a b c)) (((cons $first $rest) (== $rest (quote (b c)))) ($bad False))))
!(GuestControl empty-view
   (case (quote ()) (((cons $first $rest) False) ($empty True))))
