!(import! &self (library json))
!(import! &self (library tensor))
!(import! &self (library postings))
!(import! &self (library embedding))
!(import! &self (library list))

!(KeyEncodingSmoke (list:factorize ("b" "a" "b")))

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

(= (numerical-smoke:boolean-mask $left $right)
   (+ (if (and (> $left 0.0) (> $right 0)) 2.0 0.0)
      (if (or (> $left 0.0) (> $right 0)) 1.0 0.0)))
!(let* (($left (tensor:from-values f64 (4) (0.0 0.0 1.0 1.0)))
         ($right (tensor:from-values i64 (4) (0 1 0 1))))
   (case (__cetta_lib_tensor_try_zip_map f64 ($left $right) numerical-smoke:boolean-mask ())
     (((Tensor:Mapped $value) (TensorBooleanSmoke (tensor:inspect $value)))
      ($other (TensorBooleanNotAdmitted $other)))))

(= (numerical-smoke:intersection-size $value)
   (size-atom (intersection-atom (cons-atom $value ()) (1 3))))
!(let $input (tensor:from-values index (4) (0 1 2 3))
   (case (__cetta_lib_tensor_try_zip_map f64 ($input) numerical-smoke:intersection-size ())
     (((Tensor:Mapped $value) (TensorIntersectionSmoke (tensor:inspect $value)))
      ($other (TensorIntersectionNotAdmitted $other)))))

!(let* (($index (postings:prepare-blocks
                   ((("f" "f") 2 1) (("f") 2 4) (("g") 3 0))))
         ($counts (postings:table-counts-before $index "f" 4 5))
         ($seen (postings:inspect-table $index))
         ($closed (postings:close $index)))
   (PostingsSmoke $counts $seen $closed))

(= (numerical-smoke:summary (Summary $count $sum) $group $uses $weight)
   (Summary (+ $count $uses) (+ $sum (* $weight $uses))))
!(let* (($index (postings:prepare-blocks ((("f" "f") 2 1))))
         ($columns (postings:fold-table-columns-before $index (("f" 0.25)) 2
           postings:group-identity (Summary 0 0.0) numerical-smoke:summary (i64 f64))))
   (PostingsColumnsSmoke (tensor:inspect (postings:column-labels $columns))
     (map-atom (postings:column-values $columns) $value (tensor:inspect $value))))

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
