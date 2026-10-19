!(import! &self re)

!(let* (($pattern (re:compile "(?i)(k)"))
        ($answer (re:captures $pattern "K"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?x:a # comment\n λ)b c"))
        ($answer (re:find $pattern "aλb c"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?i)[^a]"))
        ($answer (re:find $pattern "A"))
        ($closed (re:close $pattern)))
   (function (return $answer)))
