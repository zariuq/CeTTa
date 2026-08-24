!(collapse
   (let $_proof
     (gslt:fn:InterningInjectiveOnV1:11
       (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1)
       (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1))
     RuntimeSourceValueIdentityAcceptedV1))
!(collapse
   (let $_proof
     (gslt:fn:InterningInjectiveOnV1:11
       (ValuesSequenceV1 ValuesTreeEmptyV1 ValuesTreeStackNilV1)
       (ValuesSequenceV1
         (ValuesTreeLeafV1 MismatchedSourceValueV1)
         ValuesTreeStackNilV1))
     RuntimeSourceValueIdentityUnsoundV1))
