!(import! &self re)

!(let* (($pattern (re:compile "name(?=:)"))
        ($answer (re:find $pattern "name:"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?<=ID:)[0-9]+"))
        ($answer (re:find $pattern "ID:123"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "cat(?!s)"))
        ($answer (re:find $pattern "cats"))
        ($closed (re:close $pattern)))
   (function (return $answer)))
