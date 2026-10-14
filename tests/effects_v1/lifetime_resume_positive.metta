; Each take consumes its own occurrence while retaining the residual.
!(let $owner (work-new (superpose (a b c)))
  (let $first (work-take $owner 1)
    (let $second (work-take $owner 1)
      (let $third (work-take $owner 2)
        (let $closed (work-cancel $owner) (LifetimeResume $first $second $third))))))
