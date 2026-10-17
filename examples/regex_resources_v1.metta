!(import! &self re)

!(let* (($pattern (re:compile "(?=(a+))\\1"))
        ($answer (re:find-bounded $pattern "aaaa" 100000))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?=(a+))\\1"))
        ($answer (re:find-bounded $pattern "aaaa" 0))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?=(a+))\\1"))
        ($answer (re:find-bounded $pattern "aaaa" 1))
        ($closed (re:close $pattern)))
   (function (return $answer)))
