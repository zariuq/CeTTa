; No answers is established by exhausted production, not a null payload.
!(let $owner (work-new (superpose ()))
  (let $taken (work-take $owner 1)
    (let $closed (work-cancel $owner) (OutcomeEmpty $taken))))
