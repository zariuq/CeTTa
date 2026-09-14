; Exercise the installed orchestration, not a second implementation in a
; test library. Both successful construction and malformed structured input
; must cross the real public entry.
!(import! &self ../../../lib/lib_bnf.metta)

!(let $parser (bnf:parser-from-text "<s> ::= \"x\"\n" "s")
   (case $parser
     (((NativeHandle $kind $identifier)
        (let $answer (langdef:parse-authored-text $parser "x")
          (let $_ (langdef:close-authored-parser $parser)
            (case $answer
              (((LangDef:AuthoredParseAccepted PlainBnfDenotedSyntaxV1
                  $_binding $_source ($tree)) PlainBnfInstalledTypedParserV1)
               ($other (PlainBnfUnexpectedInstalledParseV1 $other)))))))
      ($other (PlainBnfUnexpectedInstalledParserV1 $other)))))

!(case (bnf:validate-document MalformedGrammar (bnf:default-authority "s"))
   (((BNF:ReadRejected grammar-input $_reason $_message)
      PlainBnfInstalledTypedMalformedRefusedV1)
    ($other (PlainBnfUnexpectedInstalledAdmissionV1 $other))))
