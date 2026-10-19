!(import! &self re)

; Names and numeric identities address the same original UTF-8 byte spans.
!(let* (($pattern (re:compile "(?<greek>λ)"))
        ($match (re:find $pattern "aλb"))
        ($text (re:group-text $match "aλb" "greek"))
        ($span (re:group-span $match 1)))
   (function (return (RegexCaptureTextV1 $text $span))))

; A participating empty capture has an empty string and an actual span.
!(let* (($pattern (re:compile "(a*)b"))
        ($match (re:find $pattern "b"))
        ($text (re:group-text $match "b" 1))
        ($span (re:group-span $match 1)))
   (function (return (RegexCaptureEmptyV1 $text $span))))

; Nonparticipation is not the empty string.
!(let* (($pattern (re:compile "(a)?b"))
        ($match (re:find $pattern "b"))
        ($text (re:group-text $match "b" 1)))
   (function (return (RegexCaptureUnmatchedV1 $text))))

; An unknown identity is a reference failure, not an unmatched group.
!(let* (($pattern (re:compile "(a)"))
        ($match (re:find $pattern "a"))
        ($span (re:group-span $match "absent")))
   (case $span
     (((re:failed re:group-reference $detail) RegexCaptureUnknownV1)
      ($other (RegexCaptureUnexpectedV1 $other)))))
