!(bind! &v (new-space))
!(_minimal-foldl-atom () () $a $b (add-atom &v $b) &self)
!(match &v $x $x)
