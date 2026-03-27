; Once quote preserves syntax, HE's stdlib unquote equation peels one layer.
!(unquote (quote (+ 1 2)))
!(unquote (quote quote))
