; Extensional conversion declarations consumed by the article compiler.
; Compositions without CertificateGSLTArticleCompilerV1 must not include this
; capability interface.

(gslt-presentation-v1 CertificateGSLTArticleConversionInterfaceV1
  (signature
    (operator oslf-external-relation-decl-v1 2)
  )
  (equations)
  (rewrites
    (rule proof-article-conversion-interface-v1
      (head
        (oslf-external-relation-decl-v1 source-proof-conversion 3))
      (body))
  ))
