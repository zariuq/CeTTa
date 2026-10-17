!(import! &self re)

; A fast frontend retains the written alternative order, not longest-match.
!(let* (($pattern (re:compile "alpha|alphabet|beta"))
        ($match (re:find $pattern "alphabet")))
   (function (return $match)))

; Numeric references select all adjacent decimal digits before CST expansion.
!(let* (($pattern (re:compile "(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)(l)\\12"))
        ($match (re:full-match $pattern "abcdefghijkll")))
   (function (return $match)))

; Failed parsing still returns its complete source diagnostic evidence.
!(re:compile "(")
