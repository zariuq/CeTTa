!(bind! &counted (new-space))
!(add-atom &counted (edge same target))
!(add-atom &counted (edge same target))
!(let $rows (collapse (match &counted (edge same target) hit))
   (println! (countprobe duplicate (size-atom $rows))))
!(let $rows (collapse (match &counted (edge absent target) hit))
   (println! (countprobe missing (size-atom $rows))))
!(let $key same
   (let $rows (collapse (match &counted (edge $key target) hit))
      (println! (countprobe resolved (size-atom $rows)))))

; The observer, not only the pattern, must license direct counting.
!(let $rows (collapse
      (match &counted (edge same target)
         (match &counted (edge absent target) never)))
   (println! (countprobe failed-template (size-atom $rows))))
!(let $rows (collapse
      (match &counted (edge same target) (superpose (left right))))
   (println! (countprobe multiple-template (size-atom $rows))))
!(let $rows (collapse
      (match &counted (edge same target)
         (let $done (add-atom &counted marker) seen)))
   (println! (countprobe effects (size-atom $rows))))
!(let $rows (collapse (match &counted marker seen))
   (println! (countprobe effect-rows (size-atom $rows))))

; Counts must reflect each mutation rather than a cached answer.
!(remove-atom &counted (edge same target))
!(let $rows (collapse (match &counted (edge same target) hit))
   (println! (countprobe removed-one (size-atom $rows))))
!(remove-atom &counted (edge same target))
!(let $rows (collapse (match &counted (edge same target) hit))
   (println! (countprobe removed-all (size-atom $rows))))
!(add-atom &counted (edge same target))
!(add-atom &counted (edge same target))
!(add-atom &counted (edge $stored target))
; A structural hit alone is not complete when a stored variable can match.
!(let $rows (collapse (match &counted (edge same target) hit))
   (println! (countprobe open-stored (size-atom $rows))))
