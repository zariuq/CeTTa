!(import! &self (library tensor))
!(import! &self (library embedding))

; Typed JSON arrays stay packed through sparse construction.
!(let* (($text "{\"coordinates\":[1,3],\"values\":[2.0,0.0]}")
         ($coordinates (tensor:from-json index (2) $text ("coordinates")))
         ($values (tensor:from-json f64 (2) $text ("values")))
         ($matrix (tensor:from-coordinates (2 3) $coordinates $values))
         ($csr (tensor:convert $matrix csr))
         ($query (tensor:from-values f64 (3) (1.0 1.0 1.0))))
   (SparseExample (tensor:at $csr 0) (tensor:at $csr 3)
     (tensor:inspect (tensor:matvec $csr $query))))

; This profile names supplied fixture vectors, not a hosted model.
!(let* (($profile (EmbeddingProfile "fixture" "supplied" "fixture-v1" 3 "plain-text-v1" "http://fixture.invalid"))
         ($documents (embedding:from-tensor $profile "document" ("France" "Fruit" "Moon")
           (tensor:from-values f64 (3 3) (1.0 0.0 0.0 0.0 1.0 0.0 0.0 0.0 1.0))))
         ($query (embedding:from-tensor $profile "query" ("capital of France")
           (tensor:from-values f64 (1 3) (1.0 0.0 0.0))))
         ($owner (embedding:retrieval-index $documents))
         ($mask (tensor:from-values bool (3) (False True True)))
         ($ranked (embedding:retrieve-cosine $owner $query $mask 2))
         ($hits (embedding:matches $ranked (france fruit moon)))
         ($closed (embedding:index-close $owner)))
   $hits)

; The receipt owns its source/query/mask independently of the index handle.
; Inspection of scalar syntax is explicit; ordinary ranking stays packed.
!(let* (($matrix (tensor:from-values f64 (3 2) (10.0 1.0 3.0 -2.0 -5.0 1.0)))
         ($query (tensor:from-values f64 (2) (1.0 2.0)))
         ($index (tensor:screen-index $matrix))
         ($receipt (tensor:screen $index $query 1))
         ($closed (tensor:close $index)))
   (let (TensorRanking Exact $indices $scores) (tensor:rerank $receipt)
     (ScreenedExample (tensor:inspect $indices) (tensor:inspect $scores))))
