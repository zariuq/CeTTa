!(import! &self re)

!(let* (($pattern (re:compile-value (re:literal 955)))
        ($match (re:find $pattern "aλb"))
        ($closed (re:close $pattern)))
   (function (return $match)))

!(let* (($pattern (re:compile-value re:epsilon))
        ($matches (re:find-all $pattern "aλ"))
        ($closed (re:close $pattern)))
   (function (return $matches)))

!(let* (($pattern (re:compile-value (re:literal 97)))
        ($match (re:find $pattern "bbb"))
        ($closed (re:close $pattern)))
   (function (return $match)))
