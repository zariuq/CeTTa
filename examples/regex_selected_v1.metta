!(import! &self re)

!(let* (($pattern (re:compile "a|ab"))
        ($first (re:find $pattern "ab"))
        ($whole (re:full-match $pattern "ab"))
        ($closed (re:close $pattern)))
   (function (return (RegexSelectionV1 $first $whole))))

!(let* (($pattern (re:compile "(a|)*"))
        ($captured (re:captures $pattern "aa"))
        ($closed (re:close $pattern)))
   (function (return $captured)))

!(re:compile "(a")
