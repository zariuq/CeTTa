; A checked certificate as compact records.  Each problem is loaded with its
; include environment, its resolution input searched, the certificate
; replayed by the trace checker and related to its sources by the provenance
; check; the records then print, and the printed text reads back to the same
; records.  Four derivations: a clause problem in one file, a clause
; problem whose clauses come through nested and selected includes, a
; formula problem whose clauses come through clausification, and the same
; formula problem with problem symbols spelled as the names the export
; would give its introduced symbols, which then take fresh names.
!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self langdef)
!(import! &self ../../../examples/atp/tptp_resolution/unification.metta)
!(import! &self ../../../examples/atp/tptp_resolution/search.metta)
!(import! &self ../../../examples/atp/tptp_resolution/trace_check.metta)
!(import! &self ../../../examples/atp/tptp_resolution/provenance_check.metta)

(= (cert:environment $root $library)
   (tptp:load-compact-include-environment-with-reader
     (tptp:open-reader strict) tagged $root $library))

(= (cert:clauses $environment)
   (case (tptp:include-environment->resolution-input $environment)
     (((TPTP:StageValue resolution-input $problem)
        (CertInput $problem ()))
      ($other (CertNoProblem $other)))))

(= (cert:formulas $environment)
   (case (tptp:include-environment->clausified-resolution-input $environment)
     (((TPTP:ClausifiedResolutionInputV1 $problem $clausifications)
        (CertInput $problem $clausifications))
      ($other (CertNoProblem $other)))))

(= (cert:certificate $problem)
   (case (tptp-resolution-example:search:prove $problem (FOSearch.Bounds 40 2000))
     (((FOSearch.Result (FOSearch.Refutation $certificate) $statistics) $certificate)
      ($other (CertNoRefutation $other)))))

(: cert:records (-> Atom Atom Atom Atom Atom %Undefined%))
(= (cert:records $tag $environment $problem $certificate $root)
   (let $files (tptp:certificate-files-of-environment $environment $root)
     (let $records (tptp:certificate-records $problem $certificate $files)
       (let $reader (tptp:open-reader strict)
         (let $text (tptp:print $reader $records)
           (let $again (tptp:read-text $reader mixed $text)
             (CertificateRecordsV1 $tag $records $text
               (if (== $records $again) same different))))))))

(= (cert:derivation $tag $root $library $input-of)
   (let $environment (cert:environment $root $library)
     (let (CertInput $problem $clausifications) ($input-of $environment)
       (let $certificate (cert:certificate $problem)
         (case ((tptp-resolution-example:check:certificate $problem $certificate)
                (tptp-resolution-example:provenance:check $environment $problem $clausifications))
           ((((FOCheck.Verified) (FOProvenance.Verified))
              (cert:records $tag $environment $problem $certificate $root))
            ($rejected (CertificateRecordsV1 $tag rejected $rejected))))))))

!(println! (cert:derivation clauses "examples/atp/tptp_resolution/nonground_refutation.p"
                            (TPTP:LibraryRoot "examples/atp/tptp_resolution") cert:clauses))
!(println! (cert:derivation includes "tests/langdef/tptp/resolution_include_v1/root.p"
                            TPTP:NoLibraryRoot cert:clauses))
!(println! (cert:derivation formulas "tests/langdef/tptp/resolution_clausification_v1/syllogism.p"
                            TPTP:NoLibraryRoot cert:formulas))
!(println! (cert:derivation fresh-names "tests/langdef/tptp/resolution_clausification_v1/fresh_names.p"
                            TPTP:NoLibraryRoot cert:formulas))
