!(import! &self re)

!(let* (($pattern (re:compile "(?<=([ab]+)([bc]+))$"))
        ($answer (re:find $pattern "abc"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile-with "(?<=([ab]+)([bc]+))$" (re:lookbehind-policy Reverse)))
        ($answer (re:find $pattern "abc"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?<=(a))b"))
        ($answer (re:find-bounded $pattern "ab" 1))
        ($closed (re:close $pattern)))
   (function (return $answer)))
