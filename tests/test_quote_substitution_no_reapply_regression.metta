; Logical let uses finite-tree unification. Quotation suspends evaluation;
; it does not hide a free occurrence of the very variable being bound.
!(assertEqualToResult
  (let $x (quote (let $x 2 $x)) (quote (tag $x)))
  ())
!(assertEqualToResult
  (let $groups (quote (size-atom $groups))
    (quote (extracted-key-count $groups)))
  ())

; A distinct destination is a finite logical binding.
!(assertEqual
  (let $piece (quote (let $x 2 $x)) (quote (tag $piece)))
  (quote (tag (quote (let $x 2 $x)))))

; chain performs one simultaneous insertion, including self-containing syntax.
!(assertEqual
  (chain (quote (let $x 2 $x)) $x (quote (tag $x)))
  (quote (tag (quote (let $x 2 $x)))))
