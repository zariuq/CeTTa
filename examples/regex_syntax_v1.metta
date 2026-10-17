!(import! &self langdef)

!(let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
        ($choice (langdef:parser-parse-text-forest $reader "a|bc*" 2000))
        ($space (langdef:parser-parse-text-forest $reader "a λ" 2000))
        ($bad (langdef:parser-parse-text-forest $reader "(a" 2000))
        ($closed (langdef:close-parser $reader)))
   (function (return (RegexSyntaxV1 $choice $space $bad))))
