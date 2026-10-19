; Two equal answers are two occurrences, not one successful answer.
!(let $owner (work-new (superpose (a a)))
  (let $taken (work-take $owner 3)
    (let $closed (work-cancel $owner) (OutcomeOccurrences $taken))))
