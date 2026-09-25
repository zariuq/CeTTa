!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)

(= (compat-public:classification $outcome)
   (case $outcome
     (((TPTP:NoParse $_byte $_rule) no-parse)
      ($records
        (if (== (get-metatype (car-atom $records)) Expression)
            accepted
            unexpected)))))
(= (compat-public:profile
     (TptpReaderArtifactInfoV1
       $_syntax $_artifact $profile $_kernel $_conflicts $_productions))
   $profile)
(= (compat-public:expanded-classification $outcome)
   (case $outcome
     (((TPTP:ExpandedDocumentV1 $_root $_inputs $_edges) expanded)
      ((TPTP:IncludeLoadFaultV1 $_fault) include-load-fault)
      ((TPTP:IncludeResolutionFaultV1 $_fault) include-resolution-fault)
      ($_ unexpected))))

!(case (tptp:open-reader strict)
   (((NativeHandle $strict-kind $strict-id)
      (case (tptp:open-reader corpus-compatible)
        (((NativeHandle $compat-kind $compat-id)
           (let $strict (NativeHandle $strict-kind $strict-id)
             (let $compat (NativeHandle $compat-kind $compat-id)
               (println!
                 (TptpCorpusCompatibilityPublicReaderV1Summary
                   (compat-public:profile
                     (tptp:reader-artifact-info $strict))
                   (compat-public:profile
                     (tptp:reader-artifact-info $compat))
                   (compat-public:classification
                     (tptp:read-text $strict "tff(ok,axiom,p)."))
                   (compat-public:classification
                     (tptp:read-text $strict
                       "tff(compat,axiom,(~ p != ~ q))."))
                   (compat-public:classification
                     (tptp:read-text $compat
                       "tff(compat,axiom,(~ p != ~ q))."))
                   (compat-public:classification
                     (tptp:read-text $strict
                       "tff(bad,axiom,(~ p != ))."))
                   (compat-public:classification
                     (tptp:read-text $compat
                       "tff(bad,axiom,(~ p != ))."))
                   (compat-public:expanded-classification
                     (tptp:read-file-expanded-with-reader
                       $strict (system:arg 0)
                       (TPTP:LibraryRoot (system:arg 1))))
                   (compat-public:expanded-classification
                     (tptp:read-file-expanded-with-reader
                       $compat (system:arg 0)
                       (TPTP:LibraryRoot (system:arg 1))))
                   (tptp:open-reader unsupported-profile))))))
         ($fault
           (println! (TptpCorpusCompatibilityPublicReaderV1Failure $fault))))))
    ($fault
      (println! (TptpCorpusCompatibilityPublicReaderV1Failure $fault)))))
