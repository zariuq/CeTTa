!(import! &self re)

; A reusable descriptor is prepared once; the relation includes overlaps.
!(let* (($pattern (re:prepare (re:pattern "a|aa")))
        ($answers (collapse (re:match-relation $pattern "aa"))))
   (function (return (RegexRelationOverlapV1 $answers))))

; Equal answers from distinct alternatives remain distinct occurrences.
!(let* (($pattern (re:compile "a|a"))
        ($answers (collapse (re:match-relation $pattern "a"))))
   (function (return (RegexRelationMultiplicityV1 $answers))))

; A deliberately small work slice retains a pending continuation, not absence.
!(let* (($pattern (re:compile "a{64}"))
        ($token (re:relation-open $pattern "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"))
        ($step (re:relation-next-bounded $token 1)))
   (case $step
     (((re:relation-pending $same-token $work) RegexRelationPendingV1)
      ($other (RegexRelationUnexpectedV1 $other)))))

; Complete exhaustion is a genuine zero-answer relation.
!(let* (($pattern (re:compile "z"))
        ($answers (collapse (re:match-relation $pattern "aa"))))
   (function (return (RegexRelationNoAnswersV1 $answers))))
