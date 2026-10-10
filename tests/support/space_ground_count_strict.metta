!(bind! &counted (new-space))
!(add-atom &counted (edge same target))
!(add-atom &counted (edge same target))
!(add-atom &counted (edge same target))
!(println! (countprobe duplicate (size (collapse (match &counted (edge same target) hit)))))
!(println! (countprobe missing (size (collapse (match &counted (edge absent target) hit)))))
!(let $key same
   (println! (countprobe resolved (size (collapse (match &counted (edge $key target) hit))))))
!(println! (countprobe failed-template
   (size (collapse (match &counted (edge same target) (match &counted (edge absent target) never))))))
!(println! (countprobe multiple-template
   (size (collapse (match &counted (edge same target) (superpose (left right)))))))
; This literal size-atom remains non-strict in HE and value-demanded in PeTTa.
!(println! (countprobe size-atom (size-atom (collapse (match &counted (edge absent target) hit)))))
