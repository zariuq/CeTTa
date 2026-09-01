!(case
   (collapse (hyperpose ((new-space) ok)))
   ((($space ok)
      (progn
        (add-atom $space (worker-local alive))
        (match $space (worker-local alive)
          branch-local-space-readable)))
    ($_ unexpected-branch-local-space-result)))
