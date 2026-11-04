; Counted-fence lexical service, before the guest grammar is invoked.
; The body is the first region ending in quote plus the opening hash count.
; It is UTF-8 source data, with no escape decoding or indentation changes.
; The executable recognizer is specified by GSLT.Parsing.RawSourceRegion;
; this declaration selects that service, not a context-free approximation.
(gslt-presentation-v1 RawSourceRegionProfileV1
  (signature (operator raw-source-region-profile 6))
  (equations)
  (rewrites
    (rule raw-source-region-profile-v1
      (head (raw-source-region-profile 114 35 34 1 first-closing utf8-verbatim))
      (body))))
