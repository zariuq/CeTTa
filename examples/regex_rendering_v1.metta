!(import! &self re)

; All ordered operations use the same matches, including empty-match advancement.
!(let* (($pattern (re:compile ""))
        ($pieces (re:split $pattern "aλ"))
        ($closed (re:close $pattern)))
   (function (return $pieces)))

; Literal replacements never interpret dollars or scan inserted text again.
!(let* (($pattern (re:compile "a*"))
        ($text (re:replace-literal $pattern "baaa" "$1"))
        ($closed (re:close $pattern)))
   (function (return $text)))

; Constructed templates make literal text and capture references explicit.
!(let* (($pattern (re:compile "(?<word>a+)"))
        ($text (re:replace-template $pattern "baa"
                 (LCons (re:text "<")
                   (LCons (re:group "word") (LCons (re:text ">") LNil)))))
        ($closed (re:close $pattern)))
   (function (return $text)))

; An unknown capture is a template fault, not an empty capture or no-match.
!(let* (($pattern (re:compile "(a)"))
        ($answer (re:replace-template $pattern "a" (LCons (re:group 2) LNil)))
        ($closed (re:close $pattern)))
   (function (return $answer)))
