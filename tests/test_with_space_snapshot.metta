!(bind! &snaptest (new-space))
!(add-atom &snaptest (foo a))
!(add-atom &snaptest (foo b))
!(with-space-snapshot $snap &snaptest
   (match $snap (foo $x)
     (match $snap (foo $y)
       (add-atom &snaptest (foo c)))))
!(match &snaptest (foo $x) $x)
