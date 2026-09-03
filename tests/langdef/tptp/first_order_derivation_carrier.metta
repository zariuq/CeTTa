!(import! &self langdef)

(= (test:term-rejected $result)
   (case $result
     (((LangDef:TermRejected $_status $_message) True)
      ($_other False))))

(= (test:status-info)
   (tstp:info-items-cons
     (tstp:info-status (tstp:status-value "thm"))
     (tstp:info-items-nil)))

(= (test:named-parent $name)
   (tstp:parent
     (tstp:source-name
       (tptp-fo:formula-name-atomic
         (tptp-fo:symbol-name (tptp-fo:symbol-lower) $name)))
     (tstp:parent-details-none)))

; E can place an anonymous inference record directly in a parent's source.
; This term therefore exercises recursive Source/Parent structure rather than
; flattening the nested `rw` inference into an invented named formula.
(= (test:nested-inference-source)
   (tstp:source-inference
     (tstp:rule-name "cn")
     (test:status-info)
     (tstp:parents-cons
       (tstp:parent
         (tstp:source-inference
           (tstp:rule-name "rw")
           (test:status-info)
           (tstp:parents-cons
             (test:named-parent "c_0_7")
             (tstp:parents-nil)))
         (tstp:parent-details-none))
       (tstp:parents-nil))))

(= (test:derivation)
   (tstp:derivation
     (tptp-fo:source-digest "fixture-revision")
     (tstp:derivation-nodes-nil)))

(= (test:formula)
   (tptp-fo:formula-atom
     (tptp-fo:atom-predicate
       (tptp-fo:symbol-name (tptp-fo:symbol-lower) "p")
       (tptp-fo:terms-nil))))

!(bind! &derivation-language
   (langdef:load-language
     "langdef/logic/tptp_first_order_derivation_v1.metta"))

!(assertEqual
   (langdef:admit-term
     &derivation-language "Source" (test:nested-inference-source))
   (LangDef:TermAdmitted))
!(assertEqual
   (langdef:admit-term
     &derivation-language "Derivation" (test:derivation))
   (LangDef:TermAdmitted))

; A Formula is a valid term of the inherited document vocabulary, but it is
; not a Source and must not cross that boundary by constructor familiarity.
!(assertEqual
   (test:term-rejected
     (langdef:admit-term
       &derivation-language "Source" (test:formula)))
   True)

; Conversely, a derivation constructor is not accepted at the inherited
; Document sort.
!(assertEqual
   (test:term-rejected
     (langdef:admit-term
       &derivation-language "Document" (test:derivation)))
   True)

!(assertEqual (langdef:close-language &derivation-language) True)
!(println! (TptpFirstOrderDerivationCarrierV1Summary 5 5 0))
