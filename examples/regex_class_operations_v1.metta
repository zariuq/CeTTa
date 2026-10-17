!(import! &self re)

!(let* (($pattern (re:compile "[a-z--[aeiou]]+"))
        ($answer (re:find $pattern "a bcdf e"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "[\\p{Greek}&&\\p{Letter}]+"))
        ($answer (re:find $pattern "aλΣ7"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?i)[a--A]"))
        ($answer (re:find $pattern "aA"))
        ($closed (re:close $pattern)))
   (function (return $answer)))
