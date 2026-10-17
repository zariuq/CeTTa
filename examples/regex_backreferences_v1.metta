!(import! &self re)

!(let* (($pattern (re:compile "^([a-z]+)-\\1$"))
        ($answer (re:find $pattern "cat-cat"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "(?i)(K)\\١"))
        ($answer (re:find $pattern "KK"))
        ($closed (re:close $pattern)))
   (function (return $answer)))

!(let* (($pattern (re:compile "^([a-z]+)-\\1$"))
        ($answer (re:find $pattern "cat-dog"))
        ($closed (re:close $pattern)))
   (function (return $answer)))
