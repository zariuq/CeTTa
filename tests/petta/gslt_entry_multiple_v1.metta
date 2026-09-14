(gslt-presentation-v1 EntryMultipleV1
  (signature (operator EntryLookupV1 2))
  (equations)
  (rewrites
    (rule entry-first-v1
      (head (EntryLookupV1 EntryKeyV1 EntryFirstV1))
      (body))
    (rule entry-second-v1
      (head (EntryLookupV1 EntryKeyV1 EntrySecondV1))
      (body))
    (rule entry-first-again-v1
      (head (EntryLookupV1 EntryKeyV1 EntryFirstV1))
      (body))))
