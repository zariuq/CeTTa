!(bind! &prog (new-space hash))
!(space-set-match-backend! &prog pathmap-imported)
!(assertEqualToResult
  (get-type &prog)
  ((Space mork)))
!(add-atom &prog
  (exec (0 demo)
    (, (BTM (edge $x $y))
       (== $x $x))
    (O (+ (path $x $y))
       (- (edge $x $y)))))
!(assertEqualToResult
  (match &prog
    (exec (0 demo)
      (, (BTM (edge $x $y))
         (== $x $x))
      (O (+ (path $x $y))
         (- (edge $x $y))))
    stored)
  (stored))
