!(bind! &@doc (new-space))
!(assertEqual (install-doc-schema! &@doc) (SchemaInstalled @doc))

; No RecognizedSourceV1 witness exists for this source.
!(assertEqual
    (add-doc &@doc
      (doc @(theorem forged) "Forged source"
           @(source forged) 0 1))
    (Rejected UnrecognizedSource @(source forged)))

!(println! (PrimeMetadataRecognitionGateSummary 2 2 0))
