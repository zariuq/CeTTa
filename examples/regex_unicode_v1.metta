!(import! &self re)

!(let* (($pattern (re:compile "\\d{١,３}"))
        ($answer (re:find $pattern "x1٢３y"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "\\p{sc=Grek}+"))
        ($answer (re:find $pattern "aλΣb"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?-u:\\d+)"))
        ($answer (re:find $pattern "٢"))
        ($closed (re:close $pattern)))
   (function (return $answer)))
