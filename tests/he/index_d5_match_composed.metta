; match over a space with an imported module reads both spaces' rows.
!(import! &self index_d5_module)
(p 1)
(p 2)
!(d5-match (collapse (match &self (p $x) (got $x))))
