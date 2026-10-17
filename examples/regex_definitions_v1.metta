!(import! &self re)

; A re-readable description works directly, without a visible handle.
!(re:find (re:pattern "a|ab") "ab")

; Captures retain their original UTF-8 byte spans and named access.
!(let* (($description (re:pattern "(?<letter>λ)"))
        ($match (re:find $description "aλb"))
        ($text (re:group-text $match "aλb" "letter")))
   (function (return (RegexDefinitionCaptureV1 $description $text))))

; Completed absence remains ordinary data.
!(re:find (re:pattern "cat|dog") "bird")

; Malformed source retains the same located diagnostic as explicit preparation.
!(case (re:find (re:pattern "(") "text")
   (((re:source-observation $evidence) RegexDefinitionRejectedV1)
    ($other (RegexDefinitionUnexpectedV1 $other))))
