!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self ../../../langdef/tptp/official_prepared_reader_v1.metta)
!(import! &self system)

!(let ($source $document)
       (tptp-prep:document (tptp:extended-bnf-file->ast (system:arg 0)))
   (case (== $source (tptp:official-syntaxbnf-digest))
     ((True
       (case (tptp-prep:compose-corpus-compatible $source $document)
         (((TPTP:CorpusCompatibleGrammarV1 $source $input)
            (let $manifest
                 (tptp-prep:manifest corpus-compatible $source)
              (let $ok (tptp-prep:save (system:arg 1) $manifest $input)
                (TptpOfficialCorpusCompatibilityPreparedV1 $ok $source))))
          ($fault $fault))))
      (False (TPTP:DigestMismatch $source)))))
