!(let $compatible
   (collapse
     (unify
       (DispatchPairV1 DispatchLeftV1 $right)
       (DispatchPairV1 DispatchLeftV1 DispatchRightV1)
       DispatchCompatibleV1
       (superpose ())))
   (DispatchCompatibilityObservationV1
     $compatible
     (DispatchPairV1 DispatchLeftV1 $right)))

!(collapse
   (unify
     (DispatchPairV1 DispatchOtherV1 $right)
     (DispatchPairV1 DispatchLeftV1 DispatchRightV1)
     DispatchUnexpectedV1
     (superpose ())))
