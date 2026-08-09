!(import! &self prime_metadata)

!(bind! &@doc (new-space))
; Effectful setup is executed as an action.  Prime Need does not use a test's
; discarded return value as an implicit sequencing device; the resulting
; policy data is asserted independently below.
!(install-doc-schema! &@doc)
!(recognize-doc-source! &@doc @(source package-alpha))
!(assertEqual
    (reify (match &@doc (MetaKeyV1 @doc $fields) $fields))
    ((FieldsV1
       (FieldV1 subject Name)
       (FieldV1 text String)
       (FieldV1 source Name)
       (FieldV1 span Span))))
!(assertEqual
    (reify
      (match &@doc (RecognizedSourceV1 $source) $source))
    (@(source package-alpha)))

(= (ordinary-result $x) (Ordinary $x))
!(assertEqual (ordinary-result payload) (Ordinary payload))

!(assertEqual
    (add-doc &@doc
      (doc @(theorem alpha) "Alpha theorem"
           @(source package-alpha) 10 25))
    (Admitted
      (MetaRecordV1 @doc
        (ValuesV1
          (ValueV1 subject @(theorem alpha))
          (ValueV1 text "Alpha theorem")
          (ValueV1 source @(source package-alpha))
          (ValueV1 span (SpanV1 10 25))))))

; Documentation is separated from the program space.  Non-reflective results
; are unchanged, while an explicit query of the doc capability sees the record.
!(assertEqual (ordinary-result payload) (Ordinary payload))
!(assertEqual
    (reify
      (match &self
        (MetaRecordV1 @doc $values) found))
    ())
!(assertEqual
    (reify
      (match &@doc
        (MetaRecordV1 @doc
          (ValuesV1
            (ValueV1 subject @(theorem alpha))
            (ValueV1 text $@(doc-text))
            (ValueV1 source @(source package-alpha))
            (ValueV1 span $@(doc-span))))
        (Pair $@(doc-text) $@(doc-span))))
    ((Pair "Alpha theorem" (SpanV1 10 25))))

; Negative admission cases are values, not silent stuck terms.
!(assertEqual
    (add-doc &@doc
      (doc @(theorem beta) 42 @(source package-alpha) 0 1))
    (Rejected SchemaMismatch @doc))
!(assertEqual
    (add-doc &@doc
      (doc @(theorem beta) "Beta theorem" @(source unknown) 0 1))
    (Rejected UnrecognizedSource @(source unknown)))
!(assertEqual
    (add-doc &@doc
      (doc @(theorem beta) "Beta theorem" @(source package-alpha) 9 2))
    (Rejected SchemaMismatch @doc))
!(assertEqual
    (add-doc &@doc (NotADoc anything))
    (Rejected MalformedDocV1 (NotADoc anything)))

!(bind! &@(doc unconfigured) (new-space))
!(assertEqual
    (add-doc &@(doc unconfigured)
      (doc @(theorem gamma) "Gamma theorem"
           @(source package-alpha) 0 1))
    (Rejected UndeclaredMetaKey @doc))

; Failed admissions leave only the one accepted document record.
!(assertEqual
    (reify
      (match &@doc (MetaRecordV1 @doc $values) $values))
    ((ValuesV1
       (ValueV1 subject @(theorem alpha))
       (ValueV1 text "Alpha theorem")
       (ValueV1 source @(source package-alpha))
       (ValueV1 span (SpanV1 10 25)))))

!(println! (PrimeUniversalNameMetadataSummary 13 13 0))
