!(import! &self ../../../lib/lib_tptp.metta)

(= (test:authored-parser-accepted
      (LangDef:AuthoredParseAccepted
        TptpOfficialExtendedBnfSourceV1
        $_binding $_source ($_value)))
   True)

(= (test:authored-parser-rejected
      (LangDef:AuthoredParseRejected
        TptpOfficialExtendedBnfSourceV1
        $_binding $_source $_offset $_expected))
   True)

(= (test:extended-bnf-result
      (TPTP:StageValue extended-bnf-ast
        (TPTP:ExtendedBnfAst $_binding $_source $document)))
   $document)

(= (test:first-entry
      (tptp-ebnf-v1:document
        (tptp-ebnf-v1:entries-cons $head $_tail) $_span))
   $head)

(= (test:two-alternative-syntax-row
      (tptp-ebnf-v1:syntax-row $_name
        (tptp-ebnf-v1:grammar-choice
          (tptp-ebnf-v1:grammar-sequences-cons $_first
            (tptp-ebnf-v1:grammar-sequences-cons $_second
              (tptp-ebnf-v1:grammar-sequences-nil)))
          $_span)
        $_row-span))
   True)

!(bind! &authored-parser
   (langdef:load-authored-parser
     "langdef/tptp/official_extended_bnf_source_v1.metta"
     "langdef/tptp/official_extended_bnf_parser_profile_v1.metta"))

!(assertEqual
   (test:authored-parser-accepted
     (langdef:parse-authored-text
       &authored-parser "<a> ::= <b>\n"))
   True)

!(assertEqual
   (test:authored-parser-rejected
     (langdef:parse-authored-text
       &authored-parser "this is not an Extended-BNF row\n"))
   True)

!(assertEqual
   (langdef:close-authored-parser &authored-parser)
   True)

!(bind! &sample
   (tptp:extended-bnf-file->ast
     "tests/langdef/tptp/official_extended_bnf_structured_samples.bnf"))
!(bind! &document (test:extended-bnf-result &sample))
!(bind! &first-entry (test:first-entry &document))

!(assertEqual
   (tptp:extended-bnf-entry-kind &first-entry)
   syntax)
!(assertEqual
   (tptp:extended-bnf-row-name &first-entry)
   (tptp-ebnf-v1:text-cons 97 (tptp-ebnf-v1:text-nil)))
!(assertEqual
   (test:two-alternative-syntax-row &first-entry)
   True)

!(println! (TptpOfficialExtendedBnfReaderV1Summary 6 6 0))
