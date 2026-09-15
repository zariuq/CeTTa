; Semantic admission for source-spanned plain-BNF grammar documents.
;
; The input contains the parsed document and an independently authored grammar
; authority: an explicit start name and a typed lexical environment.  Rule and
; lexical namespaces are disjoint.  Every reference must resolve to exactly one
; of them.  Definition and diagnostic order follows authored occurrence order.
; This component deliberately knows nothing about ParserPack, GLL, GLR, or C.

(gslt-presentation-v1 PlainBnfSemanticAdmissionV1
  (signature
    (operator different 2)
    (operator ground-integer-less 2)
    (operator ground-integer-not-less 2)
    (operator ground-unicode-scalar 1)
    (operator ground-non-unicode-scalar 1)

    (operator bnf-v1:source-span 2)
    (operator bnf-v1:document 2)
    (operator bnf-v1:entries-cons 2)
    (operator bnf-v1:rule 3)
    (operator bnf-v1:comment 2)
    (operator bnf-v1:blank 1)
    (operator bnf-v1:expression 2)
    (operator bnf-v1:alternatives-cons 2)
    (operator bnf-v1:alternative 2)
    (operator bnf-v1:elements-cons 2)
    (operator bnf-v1:reference 2)
    (operator bnf-v1:literal 2)
    (operator bnf-v1:grammar-input 2)
    (operator bnf-v1:grammar-authority 2)
    (operator bnf-v1:start 1)
    (operator bnf-v1:lexical-environment 1)
    (operator bnf-v1:lexical-declarations-cons 2)
    (operator bnf-v1:lexical-declaration 5)
    (operator bnf-v1:lexical-points 1)
    (operator bnf-v1:lexical-except 1)
    (operator bnf-v1:scalars-cons 2)
    (operator bnf-v1:lexical-origin 2)

    (operator BNFValidateGrammarV1 2)
    (operator BNFCollectDefinitionsV1 4)
    (operator BNFDefinitionLookupV1 3)
    (operator BNFDefinitionStepV1 7)
    (operator BNFAppendDefinitionV1 3)
    (operator BNFAppendDiagnosticsV1 3)
    (operator BNFLexicalReferenceLookupV1 3)
    (operator BNFLexicalClassLookupV1 3)
    (operator BNFLexicalLabelLookupV1 3)
    (operator BNFValidateLexicalsV1 3)
    (operator BNFLexicalReferenceDiagnosticV1 4)
    (operator BNFLexicalClassDiagnosticV1 4)
    (operator BNFLexicalLabelDiagnosticV1 4)
    (operator BNFLexicalRuleConflictDiagnosticV1 4)
    (operator BNFLexicalMatcherDiagnosticV1 4)
    (operator BNFValidateScalarListV1 3)
    (operator BNFScalarDiagnosticV1 3)
    (operator BNFScalarOrderDiagnosticV1 4)
    (operator BNFLexicalReferenceNameDiagnosticV1 3)
    (operator BNFLexicalStringDiagnosticV1 4)
    (operator BNFScanEntryReferencesV1 4)
    (operator BNFScanExpressionReferencesV1 5)
    (operator BNFScanAlternativesReferencesV1 5)
    (operator BNFScanElementsReferencesV1 5)
    (operator BNFScanElementReferencesV1 5)
    (operator BNFReferenceAfterDefinitionLookupV1 6)
    (operator BNFReferenceAfterLexicalLookupV1 5)
    (operator BNFStartAfterLookupV1 4)
    (operator BNFFinishValidationV1 6)

    (operator BNFDefinitionsConsV1 2)
    (operator BNFDefinitionV1 3)
    (operator BNFDefinitionFoundV1 2)
    (operator BNFLexicalFoundV1 1)
    (operator BNFStartSomeV1 2)
    (operator BNFStartInvalidV1 1)
    (operator BNFDiagnosticsConsV1 2)
    (operator BNFNoDefinitionsV1 1)
    (operator BNFDuplicateDefinitionV1 3)
    (operator BNFUnresolvedReferenceV1 3)
    (operator BNFUnknownStartV1 1)
    (operator BNFLexicalRuleNameConflictV1 3)
    (operator BNFDuplicateLexicalReferenceV1 3)
    (operator BNFDuplicateLexicalClassV1 3)
    (operator BNFDuplicateLexicalLabelV1 3)
    (operator BNFEmptyLexicalReferenceV1 1)
    (operator BNFEmptyLexicalFieldV1 3)
    (operator BNFEmptyLexicalClassV1 2)
    (operator BNFInvalidUnicodeScalarV1 2)
    (operator BNFNonIncreasingLexicalScalarsV1 3)
    (operator BNFDeclarationValidationAcceptedV1 3)
    (operator BNFDeclarationValidationRefusedV1 1))
  (equations)
  (rewrites
    ; Exact finite lookup.  The first matching definition is authoritative;
    ; disequality is the shared ground structural relation.
    (rule bnf-definition-lookup-missing-v1
      (head
        (BNFDefinitionLookupV1
          ?name BNFDefinitionsNilV1 BNFDefinitionMissingV1))
      (body))
    (rule bnf-definition-lookup-found-v1
      (head
        (BNFDefinitionLookupV1
          ?name
          (BNFDefinitionsConsV1
            (BNFDefinitionV1 ?name ?expression ?firstSpan) ?tail)
          (BNFDefinitionFoundV1 ?expression ?firstSpan)))
      (body))
    (rule bnf-definition-lookup-tail-v1
      (head
        (BNFDefinitionLookupV1
          ?name
          (BNFDefinitionsConsV1
            (BNFDefinitionV1 ?other ?expression ?firstSpan) ?tail)
          ?result))
      (body
        (different ?name ?other)
        (BNFDefinitionLookupV1 ?name ?tail ?result)))

    ; Appending instead of prepending retains definition source order.
    (rule bnf-append-definition-nil-v1
      (head
        (BNFAppendDefinitionV1
          BNFDefinitionsNilV1 ?definition
          (BNFDefinitionsConsV1 ?definition BNFDefinitionsNilV1)))
      (body))
    (rule bnf-append-definition-cons-v1
      (head
        (BNFAppendDefinitionV1
          (BNFDefinitionsConsV1 ?head ?tail) ?definition
          (BNFDefinitionsConsV1 ?head ?after)))
      (body
        (BNFAppendDefinitionV1 ?tail ?definition ?after)))

    (rule bnf-append-diagnostics-nil-v1
      (head
        (BNFAppendDiagnosticsV1
          BNFDiagnosticsNilV1 ?right ?right))
      (body))
    (rule bnf-append-diagnostics-cons-v1
      (head
        (BNFAppendDiagnosticsV1
          (BNFDiagnosticsConsV1 ?head ?tail) ?right
          (BNFDiagnosticsConsV1 ?head ?after)))
      (body
        (BNFAppendDiagnosticsV1 ?tail ?right ?after)))

    ; Fresh definitions extend the ordered environment.  Start selection is
    ; separate structured authority and is never inferred from list position.
    (rule bnf-definition-step-fresh-v1
      (head
        (BNFDefinitionStepV1
          ?name ?span ?expression BNFDefinitionMissingV1
          ?before ?after
          BNFDiagnosticsNilV1))
      (body
        (BNFAppendDefinitionV1
          ?before (BNFDefinitionV1 ?name ?expression ?span) ?after)))
    (rule bnf-definition-step-duplicate-v1
      (head
        (BNFDefinitionStepV1
          ?name ?duplicateSpan ?expression
          (BNFDefinitionFoundV1 ?firstExpression ?firstSpan)
          ?definitions ?definitions
          (BNFDiagnosticsConsV1
            (BNFDuplicateDefinitionV1
              ?name ?firstSpan ?duplicateSpan)
            BNFDiagnosticsNilV1)))
      (body))

    ; The definition pass threads only the source-ordered environment.
    (rule bnf-collect-definitions-nil-v1
      (head
        (BNFCollectDefinitionsV1
          (metta-nullary bnf-v1:entries-nil)
          ?definitions ?definitions BNFDiagnosticsNilV1))
      (body))
    (rule bnf-collect-definitions-comment-v1
      (head
        (BNFCollectDefinitionsV1
          (bnf-v1:entries-cons
            (bnf-v1:comment ?text ?span) ?tail)
          ?before ?after ?diagnostics))
      (body
        (BNFCollectDefinitionsV1
          ?tail ?before ?after ?diagnostics)))
    (rule bnf-collect-definitions-blank-v1
      (head
        (BNFCollectDefinitionsV1
          (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail)
          ?before ?after ?diagnostics))
      (body
        (BNFCollectDefinitionsV1
          ?tail ?before ?after ?diagnostics)))
    (rule bnf-collect-definitions-rule-v1
      (head
        (BNFCollectDefinitionsV1
          (bnf-v1:entries-cons
            (bnf-v1:rule ?name ?expression ?span) ?tail)
          ?before ?after ?diagnostics))
      (body
        (BNFDefinitionLookupV1 ?name ?before ?lookup)
        (BNFDefinitionStepV1
          ?name ?span ?expression ?lookup ?before
          ?next ?currentDiagnostics)
        (BNFCollectDefinitionsV1
          ?tail ?next ?after ?tailDiagnostics)
        (BNFAppendDiagnosticsV1
          ?currentDiagnostics ?tailDiagnostics ?diagnostics)))

    ; A lexical environment is ordinary typed data.  Three independent keys
    ; are unique: BNF reference name, class name, and rule label.  The source
    ; reference name is also the sort generated by denotation.
    ; Lookup returns the complete first occurrence so diagnostics retain both
    ; origins rather than collapsing duplicate declarations.
    (rule bnf-lexical-reference-lookup-missing-v1
      (head (BNFLexicalReferenceLookupV1
        ?name (metta-nullary bnf-v1:lexical-declarations-nil)
        BNFLexicalMissingV1))
      (body))
    (rule bnf-lexical-reference-lookup-found-v1
      (head (BNFLexicalReferenceLookupV1
        ?name
        (bnf-v1:lexical-declarations-cons
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?label ?origin)
          ?tail)
        (BNFLexicalFoundV1
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?label ?origin))))
      (body))
    (rule bnf-lexical-reference-lookup-tail-v1
      (head (BNFLexicalReferenceLookupV1
        ?name
        (bnf-v1:lexical-declarations-cons
          (bnf-v1:lexical-declaration
            ?other ?class ?matcher ?label ?origin)
          ?tail)
        ?result))
      (body
        (different ?name ?other)
        (BNFLexicalReferenceLookupV1 ?name ?tail ?result)))

    (rule bnf-lexical-class-lookup-missing-v1
      (head (BNFLexicalClassLookupV1
        ?class (metta-nullary bnf-v1:lexical-declarations-nil)
        BNFLexicalMissingV1))
      (body))
    (rule bnf-lexical-class-lookup-found-v1
      (head (BNFLexicalClassLookupV1
        ?class
        (bnf-v1:lexical-declarations-cons
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?label ?origin)
          ?tail)
        (BNFLexicalFoundV1
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?label ?origin))))
      (body))
    (rule bnf-lexical-class-lookup-tail-v1
      (head (BNFLexicalClassLookupV1
        ?class
        (bnf-v1:lexical-declarations-cons
          (bnf-v1:lexical-declaration
            ?name ?other ?matcher ?label ?origin)
          ?tail)
        ?result))
      (body
        (different ?class ?other)
        (BNFLexicalClassLookupV1 ?class ?tail ?result)))

    (rule bnf-lexical-label-lookup-missing-v1
      (head (BNFLexicalLabelLookupV1
        ?label (metta-nullary bnf-v1:lexical-declarations-nil)
        BNFLexicalMissingV1))
      (body))
    (rule bnf-lexical-label-lookup-found-v1
      (head (BNFLexicalLabelLookupV1
        ?label
        (bnf-v1:lexical-declarations-cons
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?label ?origin)
          ?tail)
        (BNFLexicalFoundV1
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?label ?origin))))
      (body))
    (rule bnf-lexical-label-lookup-tail-v1
      (head (BNFLexicalLabelLookupV1
        ?label
        (bnf-v1:lexical-declarations-cons
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?other ?origin)
          ?tail)
        ?result))
      (body
        (different ?label ?other)
        (BNFLexicalLabelLookupV1 ?label ?tail ?result)))

    (rule bnf-lexical-reference-name-empty-v1
      (head (BNFLexicalReferenceNameDiagnosticV1
        (metta-nullary bnf-v1:text-nil) ?origin
        (BNFDiagnosticsConsV1
          (BNFEmptyLexicalReferenceV1 ?origin) BNFDiagnosticsNilV1)))
      (body))
    (rule bnf-lexical-reference-name-nonempty-v1
      (head (BNFLexicalReferenceNameDiagnosticV1
        (bnf-v1:text-cons ?head ?tail) ?origin BNFDiagnosticsNilV1))
      (body))

    (rule bnf-lexical-string-empty-v1
      (head (BNFLexicalStringDiagnosticV1
        ?field "" ?origin
        (BNFDiagnosticsConsV1
          (BNFEmptyLexicalFieldV1 ?field "" ?origin)
          BNFDiagnosticsNilV1)))
      (body))
    (rule bnf-lexical-string-nonempty-v1
      (head (BNFLexicalStringDiagnosticV1
        ?field ?value ?origin BNFDiagnosticsNilV1))
      (body (different ?value "")))

    (rule bnf-lexical-matcher-empty-points-v1
      (head (BNFLexicalMatcherDiagnosticV1
        ?class (bnf-v1:lexical-points
          (metta-nullary bnf-v1:scalars-nil)) ?origin
        (BNFDiagnosticsConsV1
          (BNFEmptyLexicalClassV1 ?class ?origin)
          BNFDiagnosticsNilV1)))
      (body))
    (rule bnf-lexical-matcher-empty-except-v1
      (head (BNFLexicalMatcherDiagnosticV1
        ?class (bnf-v1:lexical-except
          (metta-nullary bnf-v1:scalars-nil)) ?origin
        BNFDiagnosticsNilV1))
      (body))
    (rule bnf-lexical-matcher-points-v1
      (head (BNFLexicalMatcherDiagnosticV1
        ?class (bnf-v1:lexical-points
          (bnf-v1:scalars-cons ?head ?tail)) ?origin
        ?diagnostics))
      (body
        (BNFValidateScalarListV1
          (bnf-v1:scalars-cons ?head ?tail) ?origin ?diagnostics)))
    (rule bnf-lexical-matcher-except-v1
      (head (BNFLexicalMatcherDiagnosticV1
        ?class (bnf-v1:lexical-except
          (bnf-v1:scalars-cons ?head ?tail)) ?origin
        ?diagnostics))
      (body
        (BNFValidateScalarListV1
          (bnf-v1:scalars-cons ?head ?tail) ?origin ?diagnostics)))

    ; Scalar membership and strict ordering are independently declared ground
    ; relations.  Their positive and complementary forms make validation
    ; total on the builtin-integer carrier without selecting a host callback.
    (rule bnf-valid-unicode-scalar-v1
      (head (BNFScalarDiagnosticV1
        ?scalar ?origin BNFDiagnosticsNilV1))
      (body (ground-unicode-scalar ?scalar)))
    (rule bnf-invalid-unicode-scalar-v1
      (head (BNFScalarDiagnosticV1
        ?scalar ?origin
        (BNFDiagnosticsConsV1
          (BNFInvalidUnicodeScalarV1 ?scalar ?origin)
          BNFDiagnosticsNilV1)))
      (body (ground-non-unicode-scalar ?scalar)))

    (rule bnf-increasing-lexical-scalars-v1
      (head (BNFScalarOrderDiagnosticV1
        ?left ?right ?origin BNFDiagnosticsNilV1))
      (body (ground-integer-less ?left ?right)))
    (rule bnf-nonincreasing-lexical-scalars-v1
      (head (BNFScalarOrderDiagnosticV1
        ?left ?right ?origin
        (BNFDiagnosticsConsV1
          (BNFNonIncreasingLexicalScalarsV1 ?left ?right ?origin)
          BNFDiagnosticsNilV1)))
      (body (ground-integer-not-less ?left ?right)))

    (rule bnf-validate-scalar-list-singleton-v1
      (head (BNFValidateScalarListV1
        (bnf-v1:scalars-cons
          ?scalar (metta-nullary bnf-v1:scalars-nil))
        ?origin ?diagnostics))
      (body
        (BNFScalarDiagnosticV1 ?scalar ?origin ?diagnostics)))
    (rule bnf-validate-scalar-list-cons-v1
      (head (BNFValidateScalarListV1
        (bnf-v1:scalars-cons
          ?head (bnf-v1:scalars-cons ?next ?tail))
        ?origin ?diagnostics))
      (body
        (BNFScalarDiagnosticV1 ?head ?origin ?headDiagnostics)
        (BNFScalarOrderDiagnosticV1
          ?head ?next ?origin ?orderDiagnostics)
        (BNFValidateScalarListV1
          (bnf-v1:scalars-cons ?next ?tail) ?origin ?tailDiagnostics)
        (BNFAppendDiagnosticsV1
          ?headDiagnostics ?orderDiagnostics ?currentDiagnostics)
        (BNFAppendDiagnosticsV1
          ?currentDiagnostics ?tailDiagnostics ?diagnostics)))

    (rule bnf-lexical-reference-unique-v1
      (head (BNFLexicalReferenceDiagnosticV1
        ?name ?origin BNFLexicalMissingV1 BNFDiagnosticsNilV1))
      (body))
    (rule bnf-lexical-reference-duplicate-v1
      (head (BNFLexicalReferenceDiagnosticV1
        ?name ?origin
        (BNFLexicalFoundV1
          (bnf-v1:lexical-declaration
            ?otherName ?otherClass ?otherMatcher ?otherLabel ?firstOrigin))
        (BNFDiagnosticsConsV1
          (BNFDuplicateLexicalReferenceV1
            ?name ?origin ?firstOrigin)
          BNFDiagnosticsNilV1)))
      (body))
    (rule bnf-lexical-class-unique-v1
      (head (BNFLexicalClassDiagnosticV1
        ?class ?origin BNFLexicalMissingV1 BNFDiagnosticsNilV1))
      (body))
    (rule bnf-lexical-class-duplicate-v1
      (head (BNFLexicalClassDiagnosticV1
        ?class ?origin
        (BNFLexicalFoundV1
          (bnf-v1:lexical-declaration
            ?otherName ?otherClass ?otherMatcher ?otherLabel ?firstOrigin))
        (BNFDiagnosticsConsV1
          (BNFDuplicateLexicalClassV1 ?class ?origin ?firstOrigin)
          BNFDiagnosticsNilV1)))
      (body))
    (rule bnf-lexical-label-unique-v1
      (head (BNFLexicalLabelDiagnosticV1
        ?label ?origin BNFLexicalMissingV1 BNFDiagnosticsNilV1))
      (body))
    (rule bnf-lexical-label-duplicate-v1
      (head (BNFLexicalLabelDiagnosticV1
        ?label ?origin
        (BNFLexicalFoundV1
          (bnf-v1:lexical-declaration
            ?otherName ?otherClass ?otherMatcher ?otherLabel ?firstOrigin))
        (BNFDiagnosticsConsV1
          (BNFDuplicateLexicalLabelV1 ?label ?origin ?firstOrigin)
          BNFDiagnosticsNilV1)))
      (body))

    (rule bnf-lexical-rule-name-disjoint-v1
      (head (BNFLexicalRuleConflictDiagnosticV1
        ?name ?origin BNFDefinitionMissingV1 BNFDiagnosticsNilV1))
      (body))
    (rule bnf-lexical-rule-name-conflict-v1
      (head (BNFLexicalRuleConflictDiagnosticV1
        ?name ?origin (BNFDefinitionFoundV1 ?expression ?ruleSpan)
        (BNFDiagnosticsConsV1
          (BNFLexicalRuleNameConflictV1 ?name ?ruleSpan ?origin)
          BNFDiagnosticsNilV1)))
      (body))

    (rule bnf-validate-lexicals-nil-v1
      (head (BNFValidateLexicalsV1
        (metta-nullary bnf-v1:lexical-declarations-nil)
        ?definitions BNFDiagnosticsNilV1))
      (body))
    (rule bnf-validate-lexicals-cons-v1
      (head (BNFValidateLexicalsV1
        (bnf-v1:lexical-declarations-cons
          (bnf-v1:lexical-declaration
            ?name ?class ?matcher ?label ?origin)
          ?tail)
        ?definitions ?diagnostics))
      (body
        (BNFLexicalReferenceNameDiagnosticV1
          ?name ?origin ?referenceNameDiagnostics)
        (BNFLexicalStringDiagnosticV1
          class-name ?class ?origin ?classNameDiagnostics)
        (BNFLexicalStringDiagnosticV1
          label ?label ?origin ?labelNameDiagnostics)
        (BNFLexicalMatcherDiagnosticV1
          ?class ?matcher ?origin ?matcherDiagnostics)
        (BNFLexicalReferenceLookupV1 ?name ?tail ?referenceLookup)
        (BNFLexicalReferenceDiagnosticV1
          ?name ?origin ?referenceLookup ?referenceDiagnostics)
        (BNFLexicalClassLookupV1 ?class ?tail ?classLookup)
        (BNFLexicalClassDiagnosticV1
          ?class ?origin ?classLookup ?classDiagnostics)
        (BNFLexicalLabelLookupV1 ?label ?tail ?labelLookup)
        (BNFLexicalLabelDiagnosticV1
          ?label ?origin ?labelLookup ?labelDiagnostics)
        (BNFDefinitionLookupV1 ?name ?definitions ?definitionLookup)
        (BNFLexicalRuleConflictDiagnosticV1
          ?name ?origin ?definitionLookup ?ruleDiagnostics)
        (BNFValidateLexicalsV1
          ?tail ?definitions ?tailDiagnostics)
        (BNFAppendDiagnosticsV1
          ?referenceNameDiagnostics ?classNameDiagnostics ?d01)
        (BNFAppendDiagnosticsV1 ?d01 ?labelNameDiagnostics ?d02)
        (BNFAppendDiagnosticsV1 ?d02 ?matcherDiagnostics ?d03)
        (BNFAppendDiagnosticsV1 ?d03 ?referenceDiagnostics ?d04)
        (BNFAppendDiagnosticsV1 ?d04 ?classDiagnostics ?d05)
        (BNFAppendDiagnosticsV1 ?d05 ?labelDiagnostics ?d06)
        (BNFAppendDiagnosticsV1 ?d06 ?ruleDiagnostics ?d07)
        (BNFAppendDiagnosticsV1 ?d07 ?tailDiagnostics ?diagnostics)))

    ; Reference validation traverses every entry, alternative, and element.
    ; It never set-collapses equal-looking occurrences.
    (rule bnf-scan-entry-references-nil-v1
      (head
        (BNFScanEntryReferencesV1
          (metta-nullary bnf-v1:entries-nil)
          ?definitions ?lexicals BNFDiagnosticsNilV1))
      (body))
    (rule bnf-scan-entry-references-comment-v1
      (head
        (BNFScanEntryReferencesV1
          (bnf-v1:entries-cons
            (bnf-v1:comment ?text ?span) ?tail)
          ?definitions ?lexicals ?diagnostics))
      (body
        (BNFScanEntryReferencesV1
          ?tail ?definitions ?lexicals ?diagnostics)))
    (rule bnf-scan-entry-references-blank-v1
      (head
        (BNFScanEntryReferencesV1
          (bnf-v1:entries-cons (bnf-v1:blank ?span) ?tail)
          ?definitions ?lexicals ?diagnostics))
      (body
        (BNFScanEntryReferencesV1
          ?tail ?definitions ?lexicals ?diagnostics)))
    (rule bnf-scan-entry-references-rule-v1
      (head
        (BNFScanEntryReferencesV1
          (bnf-v1:entries-cons
            (bnf-v1:rule ?name ?expression ?span) ?tail)
          ?definitions ?lexicals ?diagnostics))
      (body
        (BNFScanExpressionReferencesV1
          ?name ?expression ?definitions ?lexicals ?currentDiagnostics)
        (BNFScanEntryReferencesV1
          ?tail ?definitions ?lexicals ?tailDiagnostics)
        (BNFAppendDiagnosticsV1
          ?currentDiagnostics ?tailDiagnostics ?diagnostics)))

    (rule bnf-scan-expression-references-v1
      (head
        (BNFScanExpressionReferencesV1
          ?owner
          (bnf-v1:expression ?alternatives ?span)
          ?definitions ?lexicals ?diagnostics))
      (body
        (BNFScanAlternativesReferencesV1
          ?owner ?alternatives ?definitions ?lexicals ?diagnostics)))
    (rule bnf-scan-alternatives-references-nil-v1
      (head
        (BNFScanAlternativesReferencesV1
          ?owner (metta-nullary bnf-v1:alternatives-nil)
          ?definitions ?lexicals BNFDiagnosticsNilV1))
      (body))
    (rule bnf-scan-alternatives-references-cons-v1
      (head
        (BNFScanAlternativesReferencesV1
          ?owner
          (bnf-v1:alternatives-cons
            (bnf-v1:alternative ?elements ?span) ?tail)
          ?definitions ?lexicals ?diagnostics))
      (body
        (BNFScanElementsReferencesV1
          ?owner ?elements ?definitions ?lexicals ?currentDiagnostics)
        (BNFScanAlternativesReferencesV1
          ?owner ?tail ?definitions ?lexicals ?tailDiagnostics)
        (BNFAppendDiagnosticsV1
          ?currentDiagnostics ?tailDiagnostics ?diagnostics)))
    (rule bnf-scan-elements-references-nil-v1
      (head
        (BNFScanElementsReferencesV1
          ?owner (metta-nullary bnf-v1:elements-nil)
          ?definitions ?lexicals BNFDiagnosticsNilV1))
      (body))
    (rule bnf-scan-elements-references-cons-v1
      (head
        (BNFScanElementsReferencesV1
          ?owner
          (bnf-v1:elements-cons ?element ?tail)
          ?definitions ?lexicals ?diagnostics))
      (body
        (BNFScanElementReferencesV1
          ?owner ?element ?definitions ?lexicals ?currentDiagnostics)
        (BNFScanElementsReferencesV1
          ?owner ?tail ?definitions ?lexicals ?tailDiagnostics)
        (BNFAppendDiagnosticsV1
          ?currentDiagnostics ?tailDiagnostics ?diagnostics)))
    (rule bnf-scan-element-literal-v1
      (head
        (BNFScanElementReferencesV1
          ?owner (bnf-v1:literal ?text ?span)
          ?definitions ?lexicals BNFDiagnosticsNilV1))
      (body))
    (rule bnf-scan-element-reference-v1
      (head
        (BNFScanElementReferencesV1
          ?owner (bnf-v1:reference ?name ?span)
          ?definitions ?lexicals ?diagnostics))
      (body
        (BNFDefinitionLookupV1 ?name ?definitions ?lookup)
        (BNFReferenceAfterDefinitionLookupV1
          ?owner ?name ?span ?lookup ?lexicals ?diagnostics)))
    (rule bnf-reference-found-v1
      (head
        (BNFReferenceAfterDefinitionLookupV1
          ?owner ?name ?span
          (BNFDefinitionFoundV1 ?expression ?firstSpan)
          ?lexicals BNFDiagnosticsNilV1))
      (body))
    (rule bnf-reference-definition-missing-v1
      (head
        (BNFReferenceAfterDefinitionLookupV1
          ?owner ?name ?span BNFDefinitionMissingV1 ?lexicals
          ?diagnostics))
      (body
        (BNFLexicalReferenceLookupV1 ?name ?lexicals ?lookup)
        (BNFReferenceAfterLexicalLookupV1
          ?owner ?name ?span ?lookup ?diagnostics)))
    (rule bnf-reference-lexical-found-v1
      (head
        (BNFReferenceAfterLexicalLookupV1
          ?owner ?name ?span (BNFLexicalFoundV1 ?declaration)
          BNFDiagnosticsNilV1))
      (body))
    (rule bnf-reference-unresolved-v1
      (head
        (BNFReferenceAfterLexicalLookupV1
          ?owner ?name ?span BNFLexicalMissingV1
          (BNFDiagnosticsConsV1
            (BNFUnresolvedReferenceV1 ?owner ?name ?span)
            BNFDiagnosticsNilV1)))
      (body))

    ; Start selection is an explicit lookup in the rule namespace.  A lexical
    ; declaration can never serve as the grammar entry category.
    (rule bnf-start-found-v1
      (head (BNFStartAfterLookupV1
        ?name (BNFDefinitionFoundV1 ?expression ?span)
        (BNFStartSomeV1 ?name ?span) BNFDiagnosticsNilV1))
      (body))
    (rule bnf-start-missing-v1
      (head (BNFStartAfterLookupV1
        ?name BNFDefinitionMissingV1
        (BNFStartInvalidV1 ?name)
        (BNFDiagnosticsConsV1
          (BNFUnknownStartV1 ?name) BNFDiagnosticsNilV1)))
      (body))

    ; Empty grammars and grammars with diagnostics are refused as data.  Only
    ; the diagnostic-free, explicit-start case exposes an admitted authority.
    (rule bnf-finish-no-definitions-v1
      (head
        (BNFFinishValidationV1
          ?documentSpan ?start BNFDefinitionsNilV1 ?lexicals ?diagnostics
          (BNFDeclarationValidationRefusedV1
            (BNFDiagnosticsConsV1
              (BNFNoDefinitionsV1 ?documentSpan) ?diagnostics))))
      (body))
    (rule bnf-finish-accepted-v1
      (head
        (BNFFinishValidationV1
          ?documentSpan (BNFStartSomeV1 ?name ?span)
          (BNFDefinitionsConsV1 ?head ?tail) ?lexicals
          BNFDiagnosticsNilV1
          (BNFDeclarationValidationAcceptedV1
            (BNFStartSomeV1 ?name ?span)
            (BNFDefinitionsConsV1 ?head ?tail) ?lexicals)))
      (body))
    (rule bnf-finish-refused-v1
      (head
        (BNFFinishValidationV1
          ?documentSpan ?start
          (BNFDefinitionsConsV1 ?definition ?definitions)
          ?lexicals
          (BNFDiagnosticsConsV1 ?head ?tail)
          (BNFDeclarationValidationRefusedV1
            (BNFDiagnosticsConsV1 ?head ?tail))))
      (body))

    (rule bnf-validate-grammar-v1
      (head
        (BNFValidateGrammarV1
          (bnf-v1:grammar-input
            (bnf-v1:document ?entries ?documentSpan)
            (bnf-v1:grammar-authority
              (bnf-v1:start ?startName)
              (bnf-v1:lexical-environment ?lexicals)))
          ?result))
      (body
        (BNFCollectDefinitionsV1
          ?entries BNFDefinitionsNilV1
          ?definitions ?definitionDiagnostics)
        (BNFValidateLexicalsV1
          ?lexicals ?definitions ?lexicalDiagnostics)
        (BNFScanEntryReferencesV1
          ?entries ?definitions ?lexicals ?referenceDiagnostics)
        (BNFDefinitionLookupV1 ?startName ?definitions ?startLookup)
        (BNFStartAfterLookupV1
          ?startName ?startLookup ?start ?startDiagnostics)
        (BNFAppendDiagnosticsV1
          ?definitionDiagnostics ?lexicalDiagnostics ?d01)
        (BNFAppendDiagnosticsV1 ?d01 ?referenceDiagnostics ?d02)
        (BNFAppendDiagnosticsV1 ?d02 ?startDiagnostics ?diagnostics)
        (BNFFinishValidationV1
          ?documentSpan ?start ?definitions ?lexicals
          ?diagnostics ?result)))))
