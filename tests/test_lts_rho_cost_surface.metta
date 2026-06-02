!(import! &self lts:rho:cost)

!(assertEqualToResult
  (collapse
    (lts:rho:cost:steps
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty))))
  (((lts:rho:cost:step
      (rho:cost:sig-mul alice bob)
      (rho:cost:signed rho:nil cont)))))

!(assertEqual
  (collapse
    (lts:rho:cost:steps
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty))))
  (collapse
    (__cetta_lib_lts_rho_cost_steps
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty)))))

!(assertEqualToResult
  (lts:rho:cost:step-cost
    (lts:rho:cost:step
      (rho:cost:sig-mul alice bob)
      (rho:cost:signed rho:nil cont)))
  ((rho:cost:sig-mul alice bob)))

!(assertEqualToResult
  (lts:rho:cost:step-state
    (lts:rho:cost:step
      (rho:cost:sig-mul alice bob)
      (rho:cost:signed rho:nil cont)))
  ((rho:cost:signed rho:nil cont)))

!(assertEqualToResult
  (let $step
    (lts:rho:cost:steps
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty)))
    (lts:rho:cost:step-cost $step))
  ((rho:cost:sig-mul alice bob)))

!(assertEqualToResult
  (let $step
    (lts:rho:cost:steps
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty)))
    (lts:rho:cost:step-state $step))
  ((rho:cost:signed rho:nil cont)))

!(assertEqualToResult
  (collapse
    (let $state
      (lts:rho:cost:transitions
        (rho:cost:par
          (rho:cost:signed
            (rho:par
              (rho:recv pay $m (rho:cost:signed rho:nil cont))
              (rho:send pay (rho:cost:signed rho:nil payload)))
            (rho:cost:sig-mul bob alice))
          (rho:cost:stack-cons alice rho:cost:stack-empty)
          (rho:cost:stack-cons bob rho:cost:stack-empty)))
      (= $state (rho:cost:signed rho:nil cont))))
  (((= (rho:cost:signed rho:nil cont)
       (rho:cost:signed rho:nil cont)))))

!(assertEqual
  (collapse
    (lts:rho:cost:transitions
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty))))
  (collapse
    (let $step
      (lts:rho:cost:steps
        (rho:cost:par
          (rho:cost:signed
            (rho:par
              (rho:recv pay $m (rho:cost:signed rho:nil cont))
              (rho:send pay (rho:cost:signed rho:nil payload)))
            (rho:cost:sig-mul bob alice))
          (rho:cost:stack-cons alice rho:cost:stack-empty)
          (rho:cost:stack-cons bob rho:cost:stack-empty)))
      (lts:rho:cost:step-state $step))))

!(assertEqualToResult
  (collapse
    (lts:trace-2 lts:rho:cost:transitions
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty))))
  (()))

!(assertEqualToResult
  (collapse
    (lts:rho:cost:trace-2
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty))))
  (()))

!(assertEqual
  (collapse
    (lts:rho:cost:trace-2
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty))))
  (collapse
    (lts:trace-2 lts:rho:cost:transitions
      (rho:cost:par
        (rho:cost:signed
          (rho:par
            (rho:recv pay $m (rho:cost:signed rho:nil cont))
            (rho:send pay (rho:cost:signed rho:nil payload)))
          (rho:cost:sig-mul bob alice))
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty)))))

!(assertEqualToResult
  (lts:is-normal-form lts:rho:cost:transitions
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)
      (rho:cost:stack-cons bob rho:cost:stack-empty)))
  (False))

!(assertEqual
  (lts:rho:cost:is-normal-form
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)
      (rho:cost:stack-cons bob rho:cost:stack-empty)))
  (lts:is-normal-form lts:rho:cost:transitions
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)
      (rho:cost:stack-cons bob rho:cost:stack-empty))))

!(assertEqualToResult
  (lts:is-can-step lts:rho:cost:transitions
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)
      (rho:cost:stack-cons bob rho:cost:stack-empty)))
  (True))

!(assertEqual
  (lts:rho:cost:is-can-step
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)
      (rho:cost:stack-cons bob rho:cost:stack-empty)))
  (lts:is-can-step lts:rho:cost:transitions
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)
      (rho:cost:stack-cons bob rho:cost:stack-empty))))

!(assertEqualToResult
  (collapse
    (lts:rho:cost:trace-2
      (rho:cost:par
        (rho:cost:signed
          (rho:recv private $m
            (rho:cost:signed
              (rho:send out (rho:cost:signed rho:nil done))
              cont))
          bob)
        (rho:cost:signed
          (rho:send private (rho:cost:signed rho:nil payload))
          alice)
        (rho:cost:signed
          (rho:recv out $n (rho:cost:signed rho:nil final))
          collector)
        (rho:cost:stack-cons alice rho:cost:stack-empty)
        (rho:cost:stack-cons bob rho:cost:stack-empty)
        (rho:cost:stack-cons cont rho:cost:stack-empty)
        (rho:cost:stack-cons collector rho:cost:stack-empty))))
  ((((rho:cost:par
       (rho:cost:signed
         (rho:recv out $n (rho:cost:signed rho:nil final))
         collector)
       (rho:cost:signed
         (rho:send out (rho:cost:signed rho:nil done))
         cont)
       (rho:cost:stack-cons collector rho:cost:stack-empty)
       (rho:cost:stack-cons cont rho:cost:stack-empty))
      (rho:cost:signed rho:nil final)))))

!(assertEqualToResult
  (lts:rho:cost:step-cost rho:poop)
  ((Error (lts:rho:cost:step-cost rho:poop)
     "expected lts:rho:cost:step record")))

!(assertEqualToResult
  (lts:rho:cost:step-cost (Error boom msg))
  ((Error (lts:rho:cost:step-cost (Error boom msg))
     "expected lts:rho:cost:step record")))

!(assertEqualToResult
  (lts:rho:cost:step-state rho:poop)
  ((Error (lts:rho:cost:step-state rho:poop)
     "expected lts:rho:cost:step record")))

!(assertEqualToResult
  (lts:rho:cost:step-state (Error boom msg))
  ((Error (lts:rho:cost:step-state (Error boom msg))
     "expected lts:rho:cost:step record")))

!(assertEqualToResult
  (lts:rho:cost:transitions rho:poop)
  ((Error (lts:rho:cost:steps rho:poop) "unsupported cost-rho term")))

!(assertEqual
  (lts:rho:cost:steps rho:poop)
  (__cetta_lib_lts_rho_cost_steps rho:poop))

!(assertEqual
  (lts:rho:cost:transitions rho:poop)
  (let $step (lts:rho:cost:steps rho:poop)
    (lts:rho:cost:step-state $step)))

!(assertEqualToResult
  (lts:trace-2 lts:rho:cost:transitions rho:poop)
  ((Error (lts:rho:cost:steps rho:poop) "unsupported cost-rho term")))

!(assertEqualToResult
  (lts:rho:cost:trace-2 rho:poop)
  ((Error (lts:rho:cost:steps rho:poop) "unsupported cost-rho term")))

!(assertEqual
  (lts:rho:cost:trace-2 rho:poop)
  (lts:trace-2 lts:rho:cost:transitions rho:poop))

!(assertEqualToResult
  (lts:is-normal-form lts:rho:cost:transitions rho:poop)
  ((Error (lts:rho:cost:steps rho:poop) "unsupported cost-rho term")))

!(assertEqualToResult
  (lts:is-can-step lts:rho:cost:transitions rho:poop)
  ((Error (lts:rho:cost:steps rho:poop) "unsupported cost-rho term")))

!(assertEqualToResult
  (lts:rho:cost:steps (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:cost:steps (Error boom msg))
  (__cetta_lib_lts_rho_cost_steps (Error boom msg)))

!(assertEqualToResult
  (lts:rho:cost:transitions (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:cost:transitions (Error boom msg))
  (let $step (lts:rho:cost:steps (Error boom msg))
    (lts:rho:cost:step-state $step)))

!(assertEqualToResult
  (lts:trace-2 lts:rho:cost:transitions (Error boom msg))
  ((Error boom msg)))

!(assertEqualToResult
  (lts:rho:cost:trace-2 (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:cost:trace-2 (Error boom msg))
  (lts:trace-2 lts:rho:cost:transitions (Error boom msg)))

!(assertEqualToResult
  (lts:is-normal-form lts:rho:cost:transitions (Error boom msg))
  ((Error boom msg)))

!(assertEqualToResult
  (lts:is-can-step lts:rho:cost:transitions (Error boom msg))
  ((Error boom msg)))

!(assertEqualToResult
  (lts:rho:cost:is-can-step
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)
      (rho:cost:stack-cons bob rho:cost:stack-empty)))
  (True))

!(assertEqualToResult
  (lts:rho:cost:is-normal-form
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)))
  (True))

!(assertEqual
  (lts:rho:cost:is-normal-form
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)))
  (lts:is-normal-form lts:rho:cost:transitions
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty))))

!(assertEqualToResult
  (lts:rho:cost:is-can-step
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)))
  (False))

!(assertEqual
  (lts:rho:cost:is-can-step
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty)))
  (lts:is-can-step lts:rho:cost:transitions
    (rho:cost:par
      (rho:cost:signed
        (rho:par
          (rho:recv pay $m (rho:cost:signed rho:nil cont))
          (rho:send pay (rho:cost:signed rho:nil payload)))
        (rho:cost:sig-mul bob alice))
      (rho:cost:stack-cons alice rho:cost:stack-empty))))

!(assertEqualToResult
  (lts:rho:cost:steps rho:poop)
  ((Error (lts:rho:cost:steps rho:poop) "unsupported cost-rho term")))

!(assertEqualToResult
  (lts:rho:cost:is-normal-form rho:poop)
  ((Error (lts:rho:cost:steps rho:poop) "unsupported cost-rho term")))

!(assertEqual
  (lts:rho:cost:is-normal-form rho:poop)
  (lts:is-normal-form lts:rho:cost:transitions rho:poop))

!(assertEqualToResult
  (lts:rho:cost:is-can-step rho:poop)
  ((Error (lts:rho:cost:steps rho:poop) "unsupported cost-rho term")))

!(assertEqual
  (lts:rho:cost:is-can-step rho:poop)
  (lts:is-can-step lts:rho:cost:transitions rho:poop))

!(assertEqualToResult
  (lts:rho:cost:is-normal-form (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:cost:is-normal-form (Error boom msg))
  (lts:is-normal-form lts:rho:cost:transitions (Error boom msg)))

!(assertEqualToResult
  (lts:rho:cost:is-can-step (Error boom msg))
  ((Error boom msg)))

!(assertEqual
  (lts:rho:cost:is-can-step (Error boom msg))
  (lts:is-can-step lts:rho:cost:transitions (Error boom msg)))

!(assertEqualToResult
  (lts:rho:cost:steps)
  ((Error (lts:rho:cost:steps) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:cost:transitions)
  ((Error (lts:rho:cost:transitions) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:cost:trace-2)
  ((Error (lts:rho:cost:trace-2) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:cost:step-cost)
  ((Error (lts:rho:cost:step-cost) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:cost:step-state)
  ((Error (lts:rho:cost:step-state) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:cost:is-normal-form)
  ((Error (lts:rho:cost:is-normal-form) IncorrectNumberOfArguments)))

!(assertEqualToResult
  (lts:rho:cost:is-can-step)
  ((Error (lts:rho:cost:is-can-step) IncorrectNumberOfArguments)))
