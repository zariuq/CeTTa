!(bind! &kb (new-space))
!(add-atom &kb (FList (FSDepth 0) (Cons ("formula" "x") Nil)))

!(assertEqualToResult
  (match &kb
    (FList (FSDepth $level) $flist)
    (== (FList (FSDepth $level) $flist)
        (FList (FSDepth 0) (Cons ("formula" "x") Nil))))
  (True))

!(assertEqualToResult
  (match &kb
    (FList (FSDepth $level) $flist)
    (== $flist (Cons ("formula" "x") Nil)))
  (True))

!(match &kb
  (FList (FSDepth $level) $flist)
  (let $_2 (add-atom &kb (FList (FSDepth $level) (Cons ("formula" "x") (Cons ("formula" "y") Nil))))
    (remove-atom &kb (FList (FSDepth $level) $flist))))

!(assertEqual
  (collapse (match &kb (FList (FSDepth $level) $flist) ($level $flist)))
  ((0 (Cons ("formula" "x") (Cons ("formula" "y") Nil)))))
