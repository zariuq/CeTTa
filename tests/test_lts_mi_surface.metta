!(import! &self lts)
!(import! &self ../../../Mettapedia/MettaKernel/Curriculum/VerifiedMeTTa/05_metta_self_interp.metta)

; Classification: derived CeTTa-side LTS surface test over the existing
; VerifiedMeTTa rule table.  This is an adapter to lib/lts.metta, not a kernel
; checker and not a second MeTTa semantics.

(= (lts:mi:step $state)
   (case $state
     (((MIState $rules $term)
        (case (mi-step $rules $term)
          (((MIStep $next) (MIState $rules $next))
           (MINoStep (superpose ())))))
      ((Error $ctx $msg) (Error $ctx $msg))
      ($bad (Error (lts:mi:step $bad) "expected MIState rules term")))))

(= (lts:mi:is-one $state)
   (case $state
     (((MIState $rules $term) (== $term (mi-one)))
      ($_ False))))
(= (lts:mi:is-two $state)
   (case $state
     (((MIState $rules $term) (== $term (mi-two)))
      ($_ False))))

!(assertEqual
  (collapse (lts:transitions lts:mi:step
    (MIState (mi-rules-add) (mi-add (mi-Z) (mi-one)))))
  (collapse (superpose ((MIState (mi-rules-add) (mi-one))))))

!(assertEqual
  (collapse (lts:transitions lts:mi:step
    (MIState (mi-rules-add) (mi-add (mi-Z) (mi-one)))))
  (collapse
    (let $next (mi-step (mi-rules-add) (mi-add (mi-Z) (mi-one)))
      (case $next
        (((MIStep $term) (MIState (mi-rules-add) $term))
         (MINoStep (superpose ())))))))

!(assertEqual
  (collapse (lts:trace-2 lts:mi:step
    (MIState (mi-rules-add) (mi-add (mi-two) (mi-one)))))
  (collapse (superpose
    (((MIState (mi-rules-add) (mi-S (mi-add (mi-one) (mi-one))))
      (MIState (mi-rules-add) (mi-S (mi-S (mi-add (mi-Z) (mi-one))))))))))

!(assertEqual
  (collapse (lts:trace-2 lts:mi:step
    (MIState (mi-rules-add) (mi-add (mi-one) (mi-one)))))
  (collapse (superpose
    (((MIState (mi-rules-add) (mi-S (mi-add (mi-Z) (mi-one))))
      (MIState (mi-rules-add) (mi-two)))))))

!(assertEqualToResult
  (lts:is-normal-form lts:mi:step (MIState (mi-rules-add) (mi-one)))
  (True))

!(assertEqualToResult
  (lts:is-can-step lts:mi:step
    (MIState (mi-rules-add) (mi-add (mi-Z) (mi-one))))
  (True))

!(assertEqualToResult
  (lts:diamond lts:mi:step lts:mi:is-one
    (MIState (mi-rules-add) (mi-add (mi-Z) (mi-one))))
  (True))

!(assertEqualToResult
  (lts:diamond lts:mi:step lts:mi:is-two
    (MIState (mi-rules-add) (mi-add (mi-Z) (mi-one))))
  (False))

!(assertEqualToResult
  (collapse (lts:transitions lts:mi:step
    (MIState (mi-rules-add) (mi-one))))
  (()))

!(assertEqualToResult
  (lts:transitions lts:mi:step not-a-mi-state)
  ((Error (lts:mi:step not-a-mi-state) "expected MIState rules term")))

!(assertEqualToResult
  (lts:transitions lts:mi:step (Error boom msg))
  ((Error boom msg)))
