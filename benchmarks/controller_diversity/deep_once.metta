; This finite recursive-first branch records the current authored-order
; witness selected by `once`.  It shows post-witness contraction, not a
; license to commit to a branch before any witness has been found.

(= (controller-diversity:deep Z) (answer deep))
(= (controller-diversity:deep (S $n))
   (controller-diversity:deep $n))

(= (controller-diversity:choice)
   (controller-diversity:deep
     (S (S (S (S (S (S (S (S Z))))))))))
(= (controller-diversity:choice) (answer shallow))

!(once (controller-diversity:choice))
