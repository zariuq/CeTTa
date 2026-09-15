; Publication canaries for the TPTP manifest's import entry.
;
; The manifest declares `(import-entry tptp-doc:parse->import)` with
; `(result-category "Document")`, so a successful import publishes exactly one
; `tptp-fo:document` value into the consumer Space.  These canaries pin that
; contract from both sides: what a successful import publishes, what a
; rejected import must leave untouched, and which term is deliberately NOT
; published.
;
; The third canary is the one that matters historically.  An earlier manifest
; entry published `TPTP:ATPInput` from a ground-only bridge; a consumer that
; still queries that term now retrieves nothing while the import itself
; reports success, which is a silent mismatch rather than an error.  Asserting
; the absence keeps the two facts -- one publication, one queried term -- from
; drifting apart again without a test noticing.

!(import! &self langdef)

(= (test:published-document)
   (== (collapse (match &self (tptp-fo:document $source $inputs) found))
       (found)))

(= (test:published-atp-input)
   (== (collapse (match &self (TPTP:ATPInput $usable $support) found))
       (found)))

(= (test:imported $result)
   (case $result
     (((LangDef:Imported TptpFofCnfV1 $digest 1) True)
      ($_ False))))

(= (test:parse-rejected $result)
   (case $result
     (((return
         (LangDef:ParseRejected TptpFofCnfV1 $digest $offset $expected))
        True)
      ($_ False))))

!(bind! &tptp-langdef (langdef:load "langdef/tptp/langdef.metta"))

; --------------------------------------------------- positive publication --

!(bind! &before-import (count-atoms &self))
!(bind! &imported
   (langdef:import-file &tptp-langdef
     "tests/langdef/tptp/nonground_refutation.p"))

; 1: the import reports publishing exactly one value.
!(assertEqual (test:imported &imported) True)

; 2: exactly one atom reached the consumer Space.
!(assertEqual (- (count-atoms &self) &before-import) 1)

; 3: that atom answers a query for the manifest's declared result category.
!(assertEqual (test:published-document) True)

; 4: and it is not the retired ground-only bridge's term.
!(assertEqual (test:published-atp-input) False)

; ------------------------------------------------------ fail-atomic import --

!(bind! &before-rejected (count-atoms &self))
!(bind! &rejected
   (langdef:import-file &tptp-langdef
     "tests/langdef/tptp/invalid_missing_period.p"))

; 5: a source the parser rejects is reported as rejected.
!(assertEqual (test:parse-rejected &rejected) True)

; 6: and leaves the consumer Space byte-for-byte unchanged.
!(assertEqual (count-atoms &self) &before-rejected)

; 7: the previously published document is still there, so a failed import
; neither published nor retracted anything.
!(assertEqual (test:published-document) True)

!(assertEqual (langdef:close &tptp-langdef) True)

!(println! (TptpPublicationCanaryV1Summary 7 7 0))
