!(import! &self ../lib/lib_bnf.metta)

; Compile once, parse several inputs, then release the parser. The same
; handle owns the prepared parser across all of these calls.
(= (reusable-bnf:status $result)
   (case $result
     (((LangDef:AuthoredParseAccepted $_grammar $_binding $_source $_values)
        accepted)
      ((LangDef:AuthoredParseRejected $_grammar $_binding $_source $_offset $_expected)
        rejected)
      ($other $other))))

!(let $parser (bnf:parser-from-text "<s> ::= \"a\" <s> |\n" "s")
   (let $empty (reusable-bnf:status (langdef:parse-authored-text $parser ""))
     (let $one (reusable-bnf:status (langdef:parse-authored-text $parser "a"))
       (let $many (reusable-bnf:status (langdef:parse-authored-text $parser "aaaaaaaa"))
         (let $bad (reusable-bnf:status (langdef:parse-authored-text $parser "ab"))
           (ReusableBnfParserV1 $empty $one $many $bad
             (langdef:close-authored-parser $parser)))))))
