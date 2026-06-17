!(import! &self lts:he)
(= (f $x) (g $x))
!(lts:he:transitions (f 5))
!(lts:he:is-normal-form (f 5))
