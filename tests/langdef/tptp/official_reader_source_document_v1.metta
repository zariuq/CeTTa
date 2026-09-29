!(import! &self ../../../lib/lib_tptp.metta)

(= (source-document-v1:run $text)
   (let $document (tptp:read-text-document $text)
     (TptpOfficialReaderSourceDocumentV1
       (tptp:source-document-identity $document)
       (tptp:source-document-digest $document)
       (== $text (tptp:source-document-text $document))
       (tptp:record-count (tptp:source-document-records $document))
       (tptp:record-occurrences $document)
       (== (tptp:source-document-records $document)
           (tptp:read-text (tptp:print-source-document $document)))
       (tptp:print-source-document (not-a-source-document))
       (tptp:record-occurrences (not-a-source-document))
       (tptp:read-text-document "fof(a,axiom,)."))))

!(println!
   (source-document-v1:run
     "fof(a,axiom,p).\nfof(a,conjecture,~p).\n"))
