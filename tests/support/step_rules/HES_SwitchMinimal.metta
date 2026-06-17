!(import! &self lts:he)
!(lts:he:transitions (switch-minimal (+ 1 2) (((+ $a $b) (got $a $b)))))
!(lts:he:is-normal-form (switch-minimal (+ 1 2) (((+ $a $b) (got $a $b)))))
