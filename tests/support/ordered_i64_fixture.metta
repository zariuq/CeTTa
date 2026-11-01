!(import! &self "space_query_fixture.metta")
!(import! &self "space_query_server.metta")
(QueryPureCallbackV1 ordered-i64:integer?)
(QueryPureCallbackV1 ordered-i64:less)
(: ordered-i64:integer? (-> Atom %Undefined%))
(= (ordered-i64:integer? (SpaceAnswerV1 (Metric $id $value) $refs))
  (if (kb:integer? $value) RelTrue RelFalse))
(: ordered-i64:less (-> Atom Atom Bool))
(= (ordered-i64:less
    (SpaceAnswerV1 (Metric $a $left) ((StoreOccurrenceV1 $ea $ka)))
    (SpaceAnswerV1 (Metric $b $right) ((StoreOccurrenceV1 $eb $kb))))
  (if (< $left $right) True
    (if (> $left $right) False (< (str:compare $ka $kb) 0))))
