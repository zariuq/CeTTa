!(import! &self re)

; Alternative order wins at one start, not the longest literal.
!(let* (($pattern (re:prepare (re:pattern "a|ab")))
        ($match (re:find $pattern "ab")))
   (function (return (RegexLiteralPriorityV1 $match))))

; An earlier start wins even when a later start has alternative zero.
!(let* (($pattern (re:prepare (re:pattern "a|baa")))
        ($match (re:find $pattern "baa")))
   (function (return (RegexLiteralStartV1 $match))))

; Full eligibility is applied before selecting the winning alternative.
!(let* (($pattern (re:prepare (re:pattern "a|ab")))
        ($match (re:full-match $pattern "ab")))
   (function (return (RegexLiteralFullV1 $match))))

; Closed absence is not an empty participating match.
!(let* (($pattern (re:prepare (re:pattern "cat|dog")))
        ($match (re:find $pattern "bird")))
   (function (return (RegexLiteralAbsentV1 $match))))
