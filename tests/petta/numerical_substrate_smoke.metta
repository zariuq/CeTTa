!(import! &self (library json))
!(import! &self (library tensor))
!(import! &self (library postings))
!(import! &self (library embedding))

!(let $value (json:parse "{\"x\":1,\"ok\":true}")
   (JsonSmoke (json:get $value "x") (json:get $value "ok")))
!(case (json:parse "{]")
   (((JsonFailureV1 $phase $status $call $message) JsonRejected)
    ($other JsonUnexpected)))

!(let $value (tensor:from-coo f64 (3) ((0 2.0) (2 0.0)))
   (TensorSmoke
     (tensor:at $value 1)
     (tensor:at $value 2)
     (tensor:inspect $value)))
!(tensor:from-values f64 (2) (1.0))

!(let* (($index (postings:prepare-blocks
                   ((("f" "f") 2 1) (("f") 2 4) (("g") 3 0))))
         ($counts (postings:table-counts-before $index "f" 4 5))
         ($seen (postings:inspect-table $index))
         ($closed (postings:close $index)))
   (PostingsSmoke $counts $seen $closed))

!(let* (($profile (EmbeddingProfile
                    "fixture" "supplied" "fixture-v1" 3
                    "plain-text-v1" "http://fixture.invalid"))
         ($documents (embedding:from-tensor $profile "document"
           ("France" "Fruit" "Moon")
           (tensor:from-values f64 (3 3)
             (1.0 0.0 0.0 0.0 1.0 0.0 0.0 0.0 1.0))))
         ($query (embedding:from-tensor $profile "query"
           ("capital of France")
           (tensor:from-values f64 (1 3) (1.0 0.0 0.0))))
         ($owner (embedding:retrieval-index $documents))
         ($mask (tensor:from-values bool (3) (False True True)))
         ($ranked (embedding:retrieve-cosine $owner $query $mask 2))
         ($hits (embedding:matches $ranked (france fruit moon)))
         ($closed (embedding:index-close $owner)))
   (EmbeddingSmoke $hits))
