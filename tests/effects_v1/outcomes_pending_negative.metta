; A bounded observation must not invent a completed empty frontier.
(= (forever) (forever))
!(let $owner (work-new (forever))
  (let $budget (work-budget $owner 8)
    (let $taken (work-take $owner 1)
      (let $closed (work-cancel $owner) (OutcomeUnfinished $taken)))))
