; superpose should execute effectful alternatives as normal evaluations.

!(let $s (new-space)
  (let $_ (superpose (
      (add-atom $s x)
      (add-atom $s y)))
    (assertEqual (collapse (match $s $v $v)) (x y))))

!(let $s (new-space)
  (let $_ (superpose (
      (add-atom $s token)
      (remove-atom $s token)))
    (assertEqual (collapse (match $s $v $v)) ())))
