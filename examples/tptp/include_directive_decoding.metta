; Decode official TPTP include nodes without reading or resolving another
; file.  The first result is the implicit-all form.  The second demonstrates
; that named selection preserves both source order and duplicate occurrences.

!(import! &self lib_tptp)

(= (example:implicit-summary
      $raw
      (TPTP:TransformationValueV1
        (tptp-include:decoded-directive
          "base" (tptp-include:implicit-all)
          (tptp-include:no-space) $raw)
        $_receipt))
   (TPTPIncludeDirectiveExample.ImplicitAll "base" RawPreserved))

(= (example:named-summary
      $raw
      (TPTP:TransformationValueV1
        (tptp-include:decoded-directive
          "library"
          (tptp-include:named-selection
            (tptp-include:names-cons "second"
              (tptp-include:names-cons "first"
                (tptp-include:names-one "second"))))
          (tptp-include:some-space "scope") $raw)
        $_receipt))
   (TPTPIncludeDirectiveExample.NamedSelection
     "library" ("second" "first" "second") "scope" RawPreserved))

(= (example:rejection-summary $result)
   (case $result
     (((TPTP:StageRejected official-include-directive
         $reason $_detail)
        (TPTPIncludeDirectiveExample.Rejected $reason))
      ($other
        (TPTPIncludeDirectiveExample.RejectionFailed $other)))))

!(let $raw
   (tptp92-ast:include:alt-1
     (tptp92-ast:file-name:alt-1
       (tptp92-ast:atomic-word:alt-1
         (tptp92-ast:token:lower-word "base")))
     (tptp92-ast:include-optionals:alt-1))
   (example:implicit-summary
     $raw (tptp:decode-include-directive 8 500000 $raw)))

!(let $raw
   (tptp92-ast:include:alt-1
     (tptp92-ast:file-name:alt-1
       (tptp92-ast:atomic-word:alt-1
         (tptp92-ast:token:lower-word "library")))
     (tptp92-ast:include-optionals:alt-3
       (tptp92-ast:formula-selection:alt-1
         (tptp92-ast:name-list:alt-2
           (tptp92-ast:name:alt-1
             (tptp92-ast:atomic-word:alt-1
               (tptp92-ast:token:lower-word "second")))
           (tptp92-ast:name-list:alt-2
             (tptp92-ast:name:alt-1
               (tptp92-ast:atomic-word:alt-1
                 (tptp92-ast:token:lower-word "first")))
             (tptp92-ast:name-list:alt-1
               (tptp92-ast:name:alt-1
                 (tptp92-ast:atomic-word:alt-1
                   (tptp92-ast:token:lower-word "second")))))))
       (tptp92-ast:space-name:alt-1
         (tptp92-ast:name:alt-1
           (tptp92-ast:atomic-word:alt-1
             (tptp92-ast:token:lower-word "scope"))))))
   (example:named-summary
     $raw (tptp:decode-include-directive 16 500000 $raw)))

!(example:rejection-summary
   (tptp:decode-include-directive 8 500000
     (tptp92-ast:include:alt-1
       (tptp92-ast:invented-file "base")
       (tptp92-ast:include-optionals:alt-1))))

!(let $raw
   (tptp92-ast:include:alt-1
     (tptp92-ast:file-name:alt-1
       (tptp92-ast:atomic-word:alt-1
         (tptp92-ast:token:lower-word "base")))
     (tptp92-ast:include-optionals:alt-1))
   (example:rejection-summary
     (tptp:decode-include-directive 0 500000 $raw)))
