!(import! &self re)

!(let* (($pattern (re:compile "(a)(b)"))
        ($answer (re:replace-template $pattern "ab" "${٠２}:$١:$$:$0"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?<名>λ)"))
        ($answer (re:replace-template $pattern "aλb" "<${名}>"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(a)"))
        ($answer (re:replace-template $pattern "a" "$12"))
        ($closed (re:close $pattern)))
   (function (return $answer)))
