!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self ../../../lib/lib_bnf.metta)
!(import! &self ../../../langdef/tptp/official_prepared_reader_v1.metta)
!(import! &self system)

!(let ($source $document)
       (tptp-prep:document (tptp:extended-bnf-file->ast (system:arg 0)))
   (case (== $source (tptp:official-syntaxbnf-digest))
     ((True
       (let $pack (tptp-prep:compose $source $document)
         (let $manifest (tptp-prep:manifest strict $source)
           (let $ok (tptp-prep:save
                      (tptp:official-reader-cache-path) $manifest $pack)
             (TptpOfficialReaderPreparedV1 $ok $source)))))
      (False (TPTP:DigestMismatch $source)))))
