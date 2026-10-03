(= (add-here $x) (add-atom &self $x))
(= (count-here) (collapse (match &self (stored $x) $x)))
(= (here) &self)
(stored 0)
