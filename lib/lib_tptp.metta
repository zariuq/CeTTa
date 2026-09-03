; Public TPTP library.
;
; The only active reader below parses the official Extended-BNF metalanguage
; into structured MeTTa data.  A pure all-family TPTP problem reader does not
; yet exist: it must be generated directly from that structured grammar into
; the standard LanguageDef and ParserPack interfaces.
;
; Every semantic transformation is executed from its declared LanguageDef
; through a generic LanguageDef runtime.  The resulting terms are admitted by
; their own LanguageDefs before they cross a public stage boundary.
;
; TPTP specifies logic and interchange formats, not a proof-search strategy.
; Accordingly this library contains no prover.  ATP examples may consume the
; admitted semantic values below, while TSTP verification will remain a
; separate, explicit operation when its semantic derivation layer is present.

!(import! &self langdef)
!(import! &self fs)

; ------------------------------------------------------------- admission --

(= (tptp:admit-stage $stage $language-path $category $value)
   (let $language (langdef:load-language $language-path)
     (case $language
       (((NativeHandle $kind $identifier)
          (let $admission
               (langdef:admit-term $language $category $value)
            (let $_ (langdef:close-language $language)
              (case $admission
                (((LangDef:TermAdmitted)
                   (TPTP:StageValue $stage $value))
                 ((LangDef:TermRejected $reason $message)
                   (TPTP:StageRejected $stage $reason $message))
                 ($_ (TPTP:StageFault $stage $admission)))))))
        ($_ (TPTP:StageFault $stage $language))))))

(= (tptp:admit-or-propagate
      $stage $language-path $category $value)
   (case $value
     (((Error $source $message)
        (TPTP:StageFault $stage $value))
      ($_
        (tptp:admit-stage
          $stage $language-path $category $value)))))

; --------------------------------------------------------------- syntax --

; Read the official TPTP Extended-BNF source as ordinary structured data.
; The native parser is compiled directly from the authored source LanguageDef
; and lexical profile.  Its source-spanned CST is then projected by authored
; equations and admitted against the independent Extended-BNF AST LanguageDef.
; This operation reads the grammar specification; it does not choose a TPTP
; logic, elaborate a formula, or run a prover.
(= (tptp:extended-bnf-file->ast $path)
   (let $parser
        (langdef:load-authored-parser
          "langdef/tptp/official_extended_bnf_source_v1.metta"
          "langdef/tptp/official_extended_bnf_parser_profile_v1.metta")
     (case $parser
       (((NativeHandle $kind $identifier)
          (let $parsed (langdef:parse-authored-file $parser $path)
            (let $_ (langdef:close-authored-parser $parser)
              (case $parsed
                (((LangDef:AuthoredParseAccepted
                    TptpOfficialExtendedBnfSourceV1
                    $binding-digest $source-digest ($cst))
                   (let $projection
                        (langdef:load-deterministic-equations
                          (LangDef:EquationSources
                            "langdef/tptp/official_extended_bnf_ast_projection_v1.metta"))
                     (case $projection
                       (((NativeHandle $projection-kind $projection-id)
                          (let $ast
                               (langdef:run-deterministic-equations
                                 $projection
                                 (LangDef:DeterministicCall
                                   tptp-ebnf-v1:project $cst))
                            (let $_
                                 (langdef:close-deterministic-equations
                                   $projection)
                              (case
                                (tptp:admit-or-propagate
                                  extended-bnf-ast
                                  "langdef/tptp/official_extended_bnf_ast_v1.metta"
                                  "Document" $ast)
                                (((TPTP:StageValue
                                    extended-bnf-ast $admitted)
                                   (TPTP:StageValue extended-bnf-ast
                                     (TPTP:ExtendedBnfAst
                                       $binding-digest $source-digest
                                       $admitted)))
                                 ($result $result))))))
                        ($_ (TPTP:StageFault
                              extended-bnf-projection $projection))))))
                  ((LangDef:AuthoredParseRejected
                    TptpOfficialExtendedBnfSourceV1
                    $binding-digest $source-digest
                    $byte-offset $expected)
                   (TPTP:ExtendedBnfRejected
                     $binding-digest $source-digest
                     $byte-offset $expected))
                  ((LangDef:AuthoredParseIncomplete
                    TptpOfficialExtendedBnfSourceV1
                    $binding-digest $source-digest
                    $outcome $detail)
                   (TPTP:ExtendedBnfIncomplete
                     $binding-digest $source-digest
                     $outcome $detail))
                  ($_ (TPTP:StageFault
                        extended-bnf-parser $parsed)))))))
        ($_ (TPTP:StageFault extended-bnf-parser $parser))))))

; Structural accessors keep the public result easy to query in every MeTTa
; profile.  They inspect only the admitted wire constructors.
(= (tptp:extended-bnf-document
      (TPTP:StageValue extended-bnf-ast
        (TPTP:ExtendedBnfAst $_binding $_source $document)))
   $document)

(= (tptp:extended-bnf-document-entry
      (tptp-ebnf-v1:document $entries $_span))
   (tptp:extended-bnf-entry-in $entries))
(= (tptp:extended-bnf-document-entry-list
      (tptp-ebnf-v1:document $entries $_span))
   $entries)
(= (tptp:extended-bnf-entry-in
      (tptp-ebnf-v1:entries-cons $head $_tail))
   $head)
(= (tptp:extended-bnf-entry-in
      (tptp-ebnf-v1:entries-cons $_head $tail))
   (tptp:extended-bnf-entry-in $tail))

(= (tptp:extended-bnf-entry-kind
      (tptp-ebnf-v1:syntax-row $_name $_rhs $_span))
   syntax)
(= (tptp:extended-bnf-entry-kind
      (tptp-ebnf-v1:semantic-row $_name $_rhs $_span))
   semantic)
(= (tptp:extended-bnf-entry-kind
      (tptp-ebnf-v1:token-row $_name $_rhs $_span))
   token)
(= (tptp:extended-bnf-entry-kind
      (tptp-ebnf-v1:character-row $_name $_rhs $_span))
   character)
(= (tptp:extended-bnf-entry-kind
      (tptp-ebnf-v1:comment $_text $_span))
   comment)
(= (tptp:extended-bnf-entry-kind
      (tptp-ebnf-v1:blank $_span))
   blank)

(= (tptp:extended-bnf-row-name
      (tptp-ebnf-v1:syntax-row $name $_rhs $_span))
   $name)
(= (tptp:extended-bnf-row-name
      (tptp-ebnf-v1:semantic-row $name $_rhs $_span))
   $name)
(= (tptp:extended-bnf-row-name
      (tptp-ebnf-v1:token-row $name $_rhs $_span))
   $name)
(= (tptp:extended-bnf-row-name
      (tptp-ebnf-v1:character-row $name $_rhs $_span))
   $name)

; The all-family TPTP problem reader is intentionally absent until the
; structured Extended-BNF AST is compiled directly to the standard syntax
; LanguageDef, lexical profile, validation obligations, and ParserPack.

; ------------------------------------------------------ include loading --

; Physical include loading is intentionally narrower than include
; resolution.  It chooses files by the documented TPTP lookup order, parses
; each distinct canonical source once, and records the choices as typed source
; documents and parent-relative bindings.  Selection, namespace semantics,
; cycle rejection, and formula expansion remain the responsibility of the
; pure resolver.

(= (tptp:include-load-member $needle $items)
   (if (== $items ())
       False
       (let ($head $rest) (decons-atom $items)
         (if (== $needle $head)
             True
             (tptp:include-load-member $needle $rest)))))

(= (tptp:include-load-documents->carrier $documents)
   (if (== $documents ())
       (tptp-include-resolution:source-documents-nil)
       (let ($head $rest) (decons-atom $documents)
         (tptp-include-resolution:source-documents-cons
           $head (tptp:include-load-documents->carrier $rest)))))

(= (tptp:include-load-bindings->carrier $bindings)
   (if (== $bindings ())
       (tptp-include-resolution:include-bindings-nil)
       (let ($head $rest) (decons-atom $bindings)
         (tptp-include-resolution:include-bindings-cons
           $head (tptp:include-load-bindings->carrier $rest)))))

(= (tptp:include-load-reverse $items)
   (tptp:include-load-reverse-acc $items ()))

(= (tptp:include-load-reverse-acc $items $accumulator)
   (if (== $items ())
       $accumulator
       (let ($head $rest) (decons-atom $items)
         (tptp:include-load-reverse-acc
           $rest (cons-atom $head $accumulator)))))

(= (tptp:include-load-state->environment
      (TPTP:IncludeLoadStateV1 $documents $bindings $_seen))
   (let $ordered-documents (tptp:include-load-reverse $documents)
     (let $ordered-bindings (tptp:include-load-reverse $bindings)
       (tptp-include-resolution:source-environment
         (tptp:include-load-documents->carrier $ordered-documents)
         (tptp:include-load-bindings->carrier $ordered-bindings)))))

(= (tptp:admit-include-resolution-environment
      $root-source $library-root $environment)
   (case
     (tptp:admit-stage
       include-resolution-environment
       "langdef/tptp/official_include_resolution_carrier_v1.metta"
       "TptpIncludeResolution:SourceEnvironment" $environment)
     (((TPTP:StageValue include-resolution-environment $admitted)
        (TPTP:IncludeResolutionEnvironmentV1
          $root-source $library-root $admitted))
      ($result $result))))

; Admit the pure resolver's complete success-or-error output.  This carrier
; includes the resolved formula order, every formula origin and include edge,
; and every declared resolution failure.  Admission checks structure only;
; the resolver LanguageDef and its agreement theorem remain separate.
(= (tptp:admit-include-resolution-result $result)
   (tptp:admit-or-propagate
     include-resolution-result
     "langdef/tptp/official_include_resolution_result_carrier_v1.metta"
     "TptpIncludeResult:ResolutionResult" $result))

; Resolve one include spelling.  The including file's directory has priority;
; the explicit library root is consulted only when the parent-relative
; candidate does not exist.  No environment variable or working-directory
; fallback is hidden in this operation.
(= (tptp:resolve-physical-include
      $from-source $requested-file TPTP:NoLibraryRoot)
   (let $local-candidate
        (fs:try-canonical-from-file $from-source $requested-file)
     (case $local-candidate
       (((FS:CanonicalPath $canonical-path)
          (TPTP:PhysicalIncludeBoundV1
            parent-relative $canonical-path))
        ((FS:CanonicalPathFault $local-message)
          (TPTP:PhysicalIncludeUnavailableV1
            $from-source $requested-file
            (TPTP:ParentRelativeFaultV1
              $requested-file $local-message)))
        ($result
          (TPTP:PhysicalIncludeLookupFaultV1 $result))))))

(= (tptp:resolve-physical-include
      $from-source $requested-file
      (TPTP:LibraryRoot $library-root))
   (tptp:resolve-physical-include-with-library
     $from-source $requested-file $library-root
     (fs:try-canonical-from-file $from-source $requested-file)))

(= (tptp:resolve-physical-include-with-library
      $from-source $requested-file $library-root $local-result)
   (case $local-result
     (((FS:CanonicalPath $local-path)
          (TPTP:PhysicalIncludeBoundV1
            parent-relative $local-path))
        ((FS:CanonicalPathFault $local-message)
          (tptp:resolve-physical-include-library-fallback
            $from-source $requested-file $library-root $local-message))
        ($result
          (TPTP:PhysicalIncludeLookupFaultV1 $result)))))

(= (tptp:resolve-physical-include-library-fallback
      $from-source $requested-file $library-root $local-message)
   (case
     (fs:try-canonical-under $library-root $requested-file)
     (((FS:CanonicalPath $library-path)
        (TPTP:PhysicalIncludeBoundV1 library-root $library-path))
      ((FS:CanonicalPathFault $library-message)
        (TPTP:PhysicalIncludeUnavailableV1
          $from-source $requested-file
          (TPTP:PhysicalIncludeCandidatesV1
            (TPTP:ParentRelativeFaultV1
              $requested-file $local-message)
            (TPTP:LibraryRootFaultV1
              $requested-file $library-message))))
      ($result
        (TPTP:PhysicalIncludeLookupFaultV1 $result)))))

; Admit one source-indexed official formula occurrence under the semantic
; carrier shared by later transformation and derivation layers.  Extracting
; occurrences from a document remains explicit: this function validates the
; resulting value, but does not invent an input-selection policy.
(= (tptp:admit-official-annotated-input $value)
   (tptp:admit-or-propagate
     official-annotated-input
     "langdef/tptp/official_semantic_carrier_v9200.metta"
     "TptpSemantic:annotated-input" $value))

; Admit an all-family official semantic derivation as inert data.  This checks
; the carrier shape only: name/occurrence uniqueness, parent topology, rule
; support, status, and calculus soundness remain separate obligations.
(= (tptp:admit-official-derivation $value)
   (tptp:admit-or-propagate
     official-derivation
     "langdef/tptp/official_semantic_carrier_v9200.metta"
     "TptpSemantic:derivation" $value))

; -------------------------------- transformations over supplied syntax --

(= (tptp:run-clause-data-plan $digest $syntax-tree)
   (let $plan
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/tptp/cnf_clause_data_bridge_v1.metta"))
     (case $plan
       (((NativeHandle $kind $identifier)
          (let $problem
               (langdef:run-deterministic-equations $plan
                 (LangDef:DeterministicCall
                   tptp:syntax-tree->problem $syntax-tree $digest))
            (let $_ (langdef:close-deterministic-equations $plan)
              (tptp:admit-or-propagate
                clause-data
                "langdef/logic/first_order_clause_data_v1.metta"
                "Problem" $problem))))
        ($_ (TPTP:StageFault clause-data $plan))))))

(= (tptp:run-document-plan $digest $syntax-tree)
   (let $plan
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/tptp/cnf_clause_data_bridge_v1.metta"
            "langdef/tptp/first_order_document_bridge_v1.metta"))
     (case $plan
       (((NativeHandle $kind $identifier)
          (let $document
               (langdef:run-deterministic-equations $plan
                 (LangDef:DeterministicCall
                   tptp-doc:syntax-tree->document $syntax-tree $digest))
            (let $_ (langdef:close-deterministic-equations $plan)
              (tptp:admit-or-propagate
                first-order-document
                "langdef/logic/tptp_first_order_document_v1.metta"
                "Document" $document))))
        ($_ (TPTP:StageFault first-order-document $plan))))))

; Admit normalized first-order TSTP derivation data.  This checks only the
; inert carrier boundary; topological and calculus-semantic verification is a
; separate operation and must not be inferred from successful admission.
(= (tptp:admit-first-order-derivation $value)
   (tptp:admit-or-propagate
     first-order-derivation
     "langdef/logic/tptp_first_order_derivation_v1.metta"
     "Derivation" $value))

; ------------------------------------------- verification-word execution --

; Execute one already-compiled derivation-check word program through the
; calculus-neutral word-machine LanguageDef.  This is deliberately a lower
; layer than tptp:verify: the caller supplies the relation rows, so a halted
; machine result is an operational receipt and not, by itself, a proof of a
; TPTP semantic objective.  A future tptp:verify must authenticate the
; official-DAG compiler, calculus service, and composition theorem before it
; may promote this receipt to semantic verification.
(= (tptp:execute-verification-word
      $step-fuel $work-limit $relation-rows $start)
   (let $language
        (langdef:load-language
          "langdef/logic/derivation_word_machine_v1.metta")
     (case $language
       (((NativeHandle $kind $identifier)
          (let $wire-identity
               (langdef:language-wire-sha256 $language)
            (let $admission
                 (langdef:admit-term $language Config $start)
              (let $outcome
                   (tptp:verification-word-after-admission
                     $language $relation-rows $step-fuel $work-limit
                     $start $admission)
                (let $_ (langdef:close-language $language)
                  (tptp:verification-word-envelope
                    $wire-identity $start $outcome))))))
        ($_
          (TPTP:VerificationWordInfrastructureFaultV1
            language-load $language))))))

(= (tptp:verification-word-after-admission
      $language $relation-rows $step-fuel $work-limit $start
      $admission)
   (case $admission
     (((LangDef:TermAdmitted)
        (tptp:verification-word-run
          $language $relation-rows $step-fuel $work-limit $start
          (TPTP:VerificationWordTracesNilV1) 0))
      ((LangDef:TermRejected $reason $message)
        (TPTP:VerificationWordStartRejectedV1 $reason $message))
      ($_
        (TPTP:VerificationWordInfrastructureFaultV1
          admission $admission)))))

(= (tptp:verification-word-envelope $wire-identity $start $outcome)
   (case $wire-identity
     (((LangDef:LanguageWireSHA256 $wire-sha256)
        (TPTP:VerificationWordExecutionV1
          $wire-sha256 $start $outcome))
      ($_
        (TPTP:VerificationWordInfrastructureFaultV1
          wire-identity $wire-identity)))))

(= (tptp:verification-word-run
      $language $relation-rows $step-fuel $work-limit
      $term $traces $steps)
   (case $term
     (((dwm:halted $outcome $nodes)
        (TPTP:VerificationWordHaltedV1
          $outcome $nodes $traces $steps))
      ($_
        (tptp:verification-word-run-active
          $language $relation-rows $step-fuel $work-limit
          $term $traces $steps)))))

; Halted terms are recognized before fuel exhaustion.  Consequently a
; terminal result reached by the final permitted transition is accepted once
; and is never executed again.
(= (tptp:verification-word-run-active
      $language $relation-rows $step-fuel $work-limit
      $term $traces $steps)
   (case (== $step-fuel 0)
     ((True
        (TPTP:VerificationWordResourceExhaustedV1
          step-fuel $term $traces $steps))
      (False
        (tptp:verification-word-transition
          $language $relation-rows $step-fuel $work-limit
          $term $traces $steps)))))

; Exactly one contextual successor is required.  Zero successors are an
; incomplete service boundary, not logical rejection; multiple successors
; are an execution fault rather than a first-result choice.
(= (tptp:verification-word-transition
      $language $relation-rows $step-fuel $work-limit
      $term $traces $steps)
   (case
     (langdef:rewrite-term-at
       $language Config Config 1 $work-limit $relation-rows $term)
     (((LangDef:ContextualTermResults LangDef:ContextComplete
         (LangDef:ContextualTermResult $target $trace))
        (tptp:verification-word-run
          $language $relation-rows (- $step-fuel 1) $work-limit
          $target
          (TPTP:VerificationWordTracesConsV1 $trace $traces)
          (+ $steps 1)))
      ((LangDef:ContextualTermResults LangDef:ContextComplete)
        (TPTP:VerificationWordIncompleteV1
          no-licensed-transition $term $traces $steps))
      ((LangDef:ContextualTermResults LangDef:ContextFuelExhausted)
        (TPTP:VerificationWordResourceExhaustedV1
          context-fuel $term $traces $steps))
      ($result
        (TPTP:VerificationWordExecutionFaultV1
          non-singleton-or-runtime-fault
          $result $term $traces $steps)))))

; ----------------------------------------------------- semantic rewrites --

; Run one declared transformation LanguageDef and retain the exact wire
; identity, request, complete contextual result, rule occurrences, and
; premise evidence.  This is operational evidence only; semantic contracts
; such as equivalence or equisatisfiability belong to the composed theorem
; and are never inferred from successful execution.
(= (tptp:language-stage-execution
      $stage $wire $source-category $target-category
      $context-fuel $work-limit $relation-rows $request)
   (let $language (langdef:load-language $wire)
     (case $language
       (((NativeHandle $kind $identifier)
          (let $wire-identity (langdef:language-wire-sha256 $language)
            (let $results
                 (langdef:rewrite-term-at
                   $language $source-category $target-category
                   $context-fuel $work-limit $relation-rows $request)
              (let $_ (langdef:close-language $language)
                (case $wire-identity
                  (((LangDef:LanguageWireSHA256 $wire-sha256)
                     (TPTP:StageExecutionV1
                       $stage $wire-sha256 $request $results))
                   ($_ (TPTP:StageFault $stage $wire-identity))))))))
        ($_ (TPTP:StageFault $stage $language))))))

; Preserve the established relational API while making its provenance-bearing
; form available to composed pipelines.
(= (tptp:language-stage-reducts
      $stage $wire $source-category $target-category
      $context-fuel $work-limit $relation-rows $request)
   (case
     (tptp:language-stage-execution
       $stage $wire $source-category $target-category
       $context-fuel $work-limit $relation-rows $request)
     (((TPTP:StageExecutionV1
         $_stage $_wire-sha256 $_request $results)
        $results)
      ($result $result))))

; A deterministic adapter is justified only by an exact singleton result.
; Zero reducts, ambiguity, exhaustion, codec failure, and runtime faults all
; remain visible and fail closed.  The receipt deliberately makes no semantic
; preservation claim by itself.
(= (tptp:require-single-stage-execution $execution)
   (case $execution
     (((TPTP:StageExecutionV1
         $stage $wire-sha256 $request
         (LangDef:ContextualTermResults LangDef:ContextComplete
           (LangDef:ContextualTermResult $target $trace)))
        (TPTP:TransformationValueV1
          $target
          (TPTP:TransformationReceiptV1
            $stage $wire-sha256 $request $target $trace)))
      ((TPTP:StageExecutionV1
         $stage $_wire-sha256 $request
         (LangDef:ContextualTermResults LangDef:ContextComplete))
        (TPTP:StageRejected $stage NoReduct $request))
      ((TPTP:StageExecutionV1
         $stage $_wire-sha256 $request
         (LangDef:ContextualTermResults LangDef:ContextFuelExhausted))
        (TPTP:StageRejected $stage ContextFuelExhausted $request))
      ((TPTP:StageExecutionV1
         $stage $_wire-sha256 $_request $other)
        (TPTP:StageRejected $stage NonSingletonOrFault $other))
      ($other $other))))

; Decode one include node from the official ParserPack abstract syntax.  The
; result retains the original node, its exact selection order and
; multiplicity, and the optional namespace spelling.  This operation does not
; read another file or choose a path: include-DAG resolution is a distinct,
; provenance-sensitive layer.
(= (tptp:include-directive-execution
      $context-fuel $work-limit $official-include)
   (tptp:language-stage-execution
     official-include-directive
     "langdef/tptp/official_include_directive_v1.metta"
     TptpInclude:DirectiveStep TptpInclude:DirectiveStep
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-include:decode-directive $official-include)))

(= (tptp:decode-include-directive
      $context-fuel $work-limit $official-include)
   (tptp:require-single-stage-execution
     (tptp:include-directive-execution
       $context-fuel $work-limit $official-include)))

; Apply the declared include-selection semantics to an already resolved
; formula list.  Physical file lookup and recursive include-DAG traversal are
; deliberately outside this operation.  Pattern-equality decisions remain an
; explicit relation provider input and are retained in the returned trace.
(= (tptp:include-selection-execution
      $context-fuel $work-limit $relation-rows $request)
   (tptp:language-stage-execution
     official-include-selection
     "langdef/tptp/official_include_selection_v1.metta"
     TptpIncludeSelection:Outcome TptpIncludeSelection:Outcome
     $context-fuel $work-limit $relation-rows $request))

(= (tptp:apply-include-selection
      $context-fuel $work-limit $relation-rows
      $target $selection $formulas)
   (tptp:require-single-stage-execution
     (tptp:include-selection-execution
       $context-fuel $work-limit $relation-rows
       (tptp-include-selection:apply $target $selection $formulas))))

; Look up a canonical source document in an already loaded finite include
; environment. The declared machine distinguishes zero, one, and multiple
; matches and returns the unique original document occurrence.
(= (tptp:include-document-lookup-execution
      $context-fuel $work-limit $relation-rows $request)
   (tptp:language-stage-execution
     official-include-environment-lookup
     "langdef/tptp/official_include_environment_lookup_v1.metta"
     TptpIncludeLookup:DocumentOutcome TptpIncludeLookup:DocumentOutcome
     $context-fuel $work-limit $relation-rows $request))

(= (tptp:lookup-include-document
      $context-fuel $work-limit $relation-rows $canonical-id $environment)
   (tptp:require-single-stage-execution
     (tptp:include-document-lookup-execution
       $context-fuel $work-limit $relation-rows
       (tptp-include-lookup:lookup-document $canonical-id $environment))))

; Resolve one include spelling relative to its parent source. The parent is
; part of the key: identical spellings under different documents may select
; different canonical targets. Missing and duplicate bindings fail closed.
(= (tptp:include-binding-lookup-execution
      $context-fuel $work-limit $relation-rows $request)
   (tptp:language-stage-execution
     official-include-environment-lookup
     "langdef/tptp/official_include_environment_lookup_v1.metta"
     TptpIncludeLookup:BindingOutcome TptpIncludeLookup:BindingOutcome
     $context-fuel $work-limit $relation-rows $request))

(= (tptp:lookup-include-binding
      $context-fuel $work-limit $relation-rows
      $from-source $requested-file $environment)
   (tptp:require-single-stage-execution
     (tptp:include-binding-lookup-execution
       $context-fuel $work-limit $relation-rows
       (tptp-include-lookup:lookup-binding
         $from-source $requested-file $environment))))

; Classify one official top-level input by following the declared micro-machine
; to a formula or include outcome.  Every transition must have exactly one
; contextual successor; no first-result choice is made.  The retained traces
; are newest-first, matching the verification-word execution convention.
(= (tptp:include-input-classification-execution
      $context-fuel $step-fuel $work-limit $request)
   (let $language
        (langdef:load-language
          "langdef/tptp/official_include_input_classification_v1.metta")
     (case $language
       (((NativeHandle $kind $identifier)
          (let $wire-identity (langdef:language-wire-sha256 $language)
            (let $outcome
                 (tptp:include-input-classification-run
                   $language $context-fuel $step-fuel $work-limit
                   $request (TPTP:TransformationTracesNilV1) 0)
              (let $_ (langdef:close-language $language)
                (tptp:include-input-classification-envelope
                  $wire-identity $request $outcome)))))
        ($_
          (TPTP:IncludeInputClassificationInfrastructureFaultV1
            language-load $language))))))

(= (tptp:include-input-classification-envelope
      $wire-identity $request $outcome)
   (case $wire-identity
     (((LangDef:LanguageWireSHA256 $wire-sha256)
        (TPTP:IncludeInputClassificationExecutionV1
          $wire-sha256 $request $outcome))
      ($_
        (TPTP:IncludeInputClassificationInfrastructureFaultV1
          wire-identity $wire-identity)))))

(= (tptp:include-input-classification-run
      $language $context-fuel $step-fuel $work-limit
      $term $traces $steps)
   (case $term
     (((tptp-include-input:formula $_formula)
        (TPTP:IncludeInputClassificationHaltedV1
          $term $traces $steps))
      ((tptp-include-input:include $_directive $_span)
        (TPTP:IncludeInputClassificationHaltedV1
          $term $traces $steps))
      ($_
        (tptp:include-input-classification-run-active
          $language $context-fuel $step-fuel $work-limit
          $term $traces $steps)))))

(= (tptp:include-input-classification-run-active
      $language $context-fuel $step-fuel $work-limit
      $term $traces $steps)
   (case (== $step-fuel 0)
     ((True
        (TPTP:IncludeInputClassificationResourceExhaustedV1
          step-fuel $term $traces $steps))
      (False
        (tptp:include-input-classification-transition
          $language $context-fuel $step-fuel $work-limit
          $term $traces $steps)))))

(= (tptp:include-input-classification-transition
      $language $context-fuel $step-fuel $work-limit
      $term $traces $steps)
   (case
     (langdef:rewrite-term-at
       $language TptpIncludeInput:Outcome TptpIncludeInput:Outcome
       $context-fuel $work-limit (LangDef:RelationRows) $term)
     (((LangDef:ContextualTermResults LangDef:ContextComplete
         (LangDef:ContextualTermResult $target $trace))
        (tptp:include-input-classification-run
          $language $context-fuel (- $step-fuel 1) $work-limit
          $target (TPTP:TransformationTracesConsV1 $trace $traces)
          (+ $steps 1)))
      ((LangDef:ContextualTermResults LangDef:ContextComplete)
        (TPTP:IncludeInputClassificationIncompleteV1
          no-licensed-transition $term $traces $steps))
      ((LangDef:ContextualTermResults LangDef:ContextFuelExhausted)
        (TPTP:IncludeInputClassificationResourceExhaustedV1
          context-fuel $term $traces $steps))
      ($result
        (TPTP:IncludeInputClassificationExecutionFaultV1
          non-singleton-or-runtime-fault
          $result $term $traces $steps)))))

; A convenient deterministic adapter justified by the exact-path theorem for
; every successfully classified official input.  The receipt is operational
; provenance and does not itself assert a semantic preservation theorem.
(= (tptp:classify-include-input
      $context-fuel $step-fuel $work-limit
      $source-id $source-digest $input-index $path $official-input)
   (let $request
        (tptp-include-input:classify
          $source-id $source-digest $input-index $path $official-input)
     (case
       (tptp:include-input-classification-execution
         $context-fuel $step-fuel $work-limit $request)
       (((TPTP:IncludeInputClassificationExecutionV1
           $wire-sha256 $_request
           (TPTP:IncludeInputClassificationHaltedV1
             $target $traces $steps))
          (TPTP:TransformationValueV1
            $target
            (TPTP:TransformationPathReceiptV1
              official-include-input-classification $wire-sha256
              $request $target $traces $steps)))
        ($result $result)))))

; Elaborate one official FOF formula into the inert named-FOF carrier.
(= (tptp:official-fof->named-reducts
      $context-fuel $work-limit $official-formula)
   (tptp:language-stage-reducts
     official-fof-to-named
     "langdef/tptp/official_fof_to_named_v1.metta"
     TptpNamedFof:Formula TptpNamedFof:Formula
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-elab:formula $official-formula)))

; Resolve named binders.  PatternEqualityDecision is deliberately supplied
; as explicit relation rows so the consumed decisions remain in the trace.
(= (tptp:named-fof->resolved-reducts
      $context-fuel $work-limit $relation-rows $named-formula)
   (tptp:language-stage-reducts
     named-fof-to-resolved
     "langdef/tptp/named_fof_to_resolved_v1.metta"
     TptpResolvedFof:Formula TptpResolvedFof:Formula
     $context-fuel $work-limit $relation-rows
     (tptp-fof-resolve:closed $named-formula)))

; Compute every positive-polarity NNF reduct licensed by the normalization
; LanguageDef.  This is intentionally a relational API: it preserves result
; order, multiplicity, rule traces, congruence evidence, and explicit
; exhaustion.  The Lean development proves singleton exactness for admitted
; resolved FOF inputs when the supplied contextual fuel is sufficient; this
; adapter does not replace that theorem with a first-result convention.
(= (tptp:resolved-fof->positive-nnf-reducts
      $context-fuel $work-limit $resolved-formula)
   (tptp:language-stage-reducts
     fof-positive-normalization
     "langdef/tptp/fof_normalization_v1.metta"
     NNFFormula NNFFormula
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-normalize:positive $resolved-formula)))

; Negative-polarity normalization computes NNF for the semantic negation of
; the resolved source formula.  Conjecture handling must select this operation
; before clausification; changing only the later TPTP role is not negation.
(= (tptp:resolved-fof->negative-nnf-reducts
      $context-fuel $work-limit $resolved-formula)
   (tptp:language-stage-reducts
     fof-negative-normalization
     "langdef/tptp/fof_normalization_v1.metta"
     NNFFormula NNFFormula
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-normalize:negative $resolved-formula)))

; Prenexing returns a source-preserving Result value containing the prenex
; form.  It remains a reduct relation even though Lean proves exact singleton
; execution for admitted inputs with sufficient fuel.
(= (tptp:nnf->prenex-reducts $context-fuel $work-limit $nnf-formula)
   (tptp:language-stage-reducts
     fof-prenex-normalization
     "langdef/tptp/fof_prenex_normalization_v1.metta"
     TptpFofPrenexNormalize:Result TptpFofPrenexNormalize:Result
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-prenex-normalize:request $nnf-formula)))

; Skolemization makes the environment, target binder depth, and fresh-symbol
; frontier explicit.  Its semantic contract is equisatisfiability, not
; equivalence; this API returns operational reducts and does not relabel that
; theorem as a runtime fact.
(= (tptp:prenex->skolem-reducts
      $context-fuel $work-limit
      $environment $target-depth $frontier $prenex-form)
   (tptp:language-stage-reducts
     fof-skolemization
     "langdef/tptp/fof_skolemization_v1.metta"
     TptpFofSkolemize:FormResult TptpFofSkolemize:FormResult
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-skolemize:form-request
       $environment $target-depth $frontier $prenex-form)))

; Open universal binders and introduce explicit definitional names from the
; supplied depth and fresh-predicate frontier.
(= (tptp:skolem-fof->definitional-naming-reducts
      $context-fuel $work-limit $depth $frontier $skolem-formula)
   (tptp:language-stage-reducts
     fof-definitional-naming
     "langdef/tptp/fof_definitional_naming_v1.metta"
     TptpFofName:OpenResult TptpFofName:OpenResult
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-name:open-request $depth $frontier $skolem-formula)))

; Turn a definitional-naming output into its clause collection.
(= (tptp:definitional-naming->cnf-reducts
      $context-fuel $work-limit $named-output)
   (tptp:language-stage-reducts
     fof-definitional-cnf-generation
     "langdef/tptp/fof_definitional_cnf_generation_v1.metta"
     TptpFofCnfGeneration:GenerateResult
     TptpFofCnfGeneration:GenerateResult
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-cnf-gen:generate-request $named-output)))

; Project source-owned occurrence and role metadata into the request consumed
; by batch generation.  The source formula, name, annotations, and span remain
; available in the admitted semantic occurrence; only the derived occurrence
; key and polarity cross this stage.  The supplied Skolem and CNF outputs must
; come from the polarity-correct transformation of this source; the metadata
; projection deliberately does not recompute that independent pipeline.
(= (tptp:official-fof->batch-request-reducts
      $context-fuel $work-limit $source $skolem-output $cnf-output)
   (tptp:language-stage-reducts
     official-fof-batch-projection
     "langdef/tptp/official_fof_batch_projection_v1.metta"
     TptpFofBatchGen:Request TptpFofBatchGen:Request
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-batch-project:request
       $source $skolem-output $cnf-output)))

; Execute an already projected batch request.  Keeping this entry point
; separate lets source-role projection feed batch generation without
; destructuring and rebuilding the request between declared stages.
(= (tptp:clausification-batch-request-reducts
      $context-fuel $work-limit $request)
   (tptp:language-stage-reducts
     fof-clausification-batch-generation
     "langdef/tptp/fof_clausification_batch_generation_v1.metta"
     TptpFofBatchGen:Request TptpFofBatch:Output
     $context-fuel $work-limit (LangDef:RelationRows)
     $request))

; Attach an explicit source occurrence and polarity to the Skolem and CNF
; evidence while assigning stable per-source clause identities.
(= (tptp:clausification-batch-reducts
      $context-fuel $work-limit $occurrence $polarity
      $skolem-output $cnf-output)
   (tptp:clausification-batch-request-reducts
     $context-fuel $work-limit
     (tptp-fof-batch-gen:request
       $occurrence $polarity $skolem-output $cnf-output)))

; Allocate canonical clause-name identities from an explicit initial
; frontier.  Rendering those identities as TPTP lexical names remains a
; separate relation supplied to the official-AST serializer.
(= (tptp:allocate-cnf-names-reducts
      $context-fuel $work-limit $batch $first-name)
   (tptp:language-stage-reducts
     fof-cnf-name-allocation
     "langdef/tptp/fof_cnf_name_allocation_v1.metta"
     TptpFofCnfNameAllocation:Request TptpFofCnfAllocated:Output
     $context-fuel $work-limit (LangDef:RelationRows)
     (tptp-fof-cnf-name-allocation:request $batch $first-name)))

; Serialize an allocated CNF batch to the official TPTP AST.  Variable,
; Skolem, definition, and clause spellings are explicit relation rows and are
; recorded in every consuming trace.
(= (tptp:official-quoted-atomic-word-pattern $lexeme)
   (PApp "tptp92-ast:atomic-word:alt-2"
     (LCons
       (PApp "tptp92-ast:token:single-quoted"
         (LCons (PApp $lexeme LNil) LNil))
       LNil)))

(= (tptp:official-quoted-name-pattern $lexeme)
   (PApp "tptp92-ast:name:alt-1"
     (LCons (tptp:official-quoted-atomic-word-pattern $lexeme) LNil)))

(= (tptp:official-quoted-functor-pattern $lexeme)
   (PApp "tptp92-ast:functor:alt-1"
     (LCons (tptp:official-quoted-atomic-word-pattern $lexeme) LNil)))

(= (tptp:official-variable-pattern $lexeme)
   (PApp "tptp92-ast:variable:alt-1"
     (LCons
       (PApp "tptp92-ast:token:upper-word"
         (LCons (PApp $lexeme LNil) LNil))
       LNil)))

; Relation providers use the generic contextual runner's Pattern wire.  This
; constructor keeps the TPTP lexical encoding local to lib_tptp while leaving
; the consumed row and its receipt explicit.
(= (tptp:official-clause-name-row $receipt $index-pattern $lexeme)
   (let $name-pattern (tptp:official-quoted-name-pattern $lexeme)
     (LangDef:RelationRow
       "tptp-cnf-official-serialization:clause-name"
       $receipt $index-pattern $name-pattern)))

(= (tptp:official-variable-row $receipt $index-pattern $lexeme)
   (let $variable-pattern (tptp:official-variable-pattern $lexeme)
     (LangDef:RelationRow
       "tptp-cnf-official-serialization:variable"
       $receipt $index-pattern $variable-pattern)))

(= (tptp:official-skolem-functor-row
      $receipt $index-pattern $lexeme)
   (let $functor-pattern (tptp:official-quoted-functor-pattern $lexeme)
     (LangDef:RelationRow
       "tptp-cnf-official-serialization:skolem-functor"
       $receipt $index-pattern $functor-pattern)))

(= (tptp:official-definition-functor-row
      $receipt $index-pattern $lexeme)
   (let $functor-pattern (tptp:official-quoted-functor-pattern $lexeme)
     (LangDef:RelationRow
       "tptp-cnf-official-serialization:definition-functor"
       $receipt $index-pattern $functor-pattern)))

(= (tptp:allocated-cnf->official-ast-reducts
      $context-fuel $work-limit $relation-rows $allocated-output)
   (tptp:language-stage-reducts
     fof-cnf-official-ast
     "langdef/tptp/fof_cnf_official_ast_v1.metta"
     TptpFofCnfOfficialSerialization:Output
     TptpFofCnfOfficialSerialization:Output
     $context-fuel $work-limit $relation-rows
     (tptp-cnf-official-serialization:serialize $allocated-output)))

; ------------------------------------------------ composed clausification --

; Clausify one admitted, source-indexed official FOF occurrence through the
; complete declared transformation chain.  The caller supplies every external
; binder decision and lexical spelling as receipt-bearing relation rows, plus
; explicit fresh-symbol frontiers.  No first-result convention is used: every
; stage must produce exactly one complete reduct or the operation fails closed.
;
; The returned receipts are operational evidence that the checked
; LanguageDefs executed.  The associated Lean theorem states that the
; polarity-indexed resolved source and semantic CNF are equisatisfiable; these
; runtime receipts are not relabelled as a proof of that theorem.
(= (tptp:clausify-official-fof $config $polarity $source)
   (case (tptp:admit-official-annotated-input $source)
     (((TPTP:StageValue official-annotated-input $admitted)
        (case ($config $admitted)
          ((((TPTP:FofClausificationConfigV1
                $context-fuel $work-limit $binder-rows
                $skolem-environment $target-depth $skolem-frontier
                $naming-depth $naming-frontier $clause-frontier
                $serialization-rows)
              (tptp-semantic:fof-input $_occurrence
                (tptp92-ast:fof-annotated:alt-1
                  $_name $_role $formula $_annotations)
                $_span))
             (tptp:clausify-official-fof-admitted
               $config $polarity $admitted $formula))
            ($_
             (TPTP:FofClausificationRejectedV1
               InvalidConfigurationOrSourceShape $config $admitted)))))
       ($result $result))))

(= (tptp:clausify-official-fof-admitted
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $_binder-rows
        $_skolem-environment $_target-depth $_skolem-frontier
        $_naming-depth $_naming-frontier $_clause-frontier
        $_serialization-rows)
      $polarity $source $formula)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         official-fof-to-named
         "langdef/tptp/official_fof_to_named_v1.metta"
         TptpNamedFof:Formula TptpNamedFof:Formula
         $context-fuel $work-limit (LangDef:RelationRows)
         (tptp-fof-elab:formula $formula)))
     (((TPTP:TransformationValueV1 $named $official-receipt)
        (tptp:clausify-after-named
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $_binder-rows
            $_skolem-environment $_target-depth $_skolem-frontier
            $_naming-depth $_naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $named $official-receipt))
       ($result $result))))

(= (tptp:clausify-after-named
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $_skolem-environment $_target-depth $_skolem-frontier
        $_naming-depth $_naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $named $official-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         named-fof-to-resolved
         "langdef/tptp/named_fof_to_resolved_v1.metta"
         TptpResolvedFof:Formula TptpResolvedFof:Formula
         $context-fuel $work-limit $binder-rows
         (tptp-fof-resolve:closed $named)))
     (((TPTP:TransformationValueV1 $resolved $resolver-receipt)
        (tptp:clausify-after-resolved
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $_skolem-environment $_target-depth $_skolem-frontier
            $_naming-depth $_naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $resolved
          $official-receipt $resolver-receipt))
       ($result $result))))

(= (tptp:clausification-normalization-execution
      $polarity $context-fuel $work-limit $resolved)
   (case $polarity
     (((tptp-fof-batch:positive)
        (tptp:language-stage-execution
          fof-positive-normalization
          "langdef/tptp/fof_normalization_v1.metta"
          NNFFormula NNFFormula
          $context-fuel $work-limit (LangDef:RelationRows)
          (tptp-fof-normalize:positive $resolved)))
      ((tptp-fof-batch:negative)
        (tptp:language-stage-execution
          fof-negative-normalization
          "langdef/tptp/fof_normalization_v1.metta"
          NNFFormula NNFFormula
          $context-fuel $work-limit (LangDef:RelationRows)
          (tptp-fof-normalize:negative $resolved)))
      ($_
        (TPTP:FofClausificationRejectedV1
          UnsupportedPolarity $polarity)))))

(= (tptp:clausify-after-resolved
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $_skolem-environment $_target-depth $_skolem-frontier
        $_naming-depth $_naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $resolved
      $official-receipt $resolver-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:clausification-normalization-execution
         $polarity $context-fuel $work-limit $resolved))
     (((TPTP:TransformationValueV1 $nnf $normalization-receipt)
        (tptp:clausify-after-nnf
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $_skolem-environment $_target-depth $_skolem-frontier
            $_naming-depth $_naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $nnf
          $official-receipt $resolver-receipt $normalization-receipt))
       ($result $result))))

(= (tptp:clausify-after-nnf
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $_skolem-environment $_target-depth $_skolem-frontier
        $_naming-depth $_naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $nnf
      $official-receipt $resolver-receipt $normalization-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-prenex-normalization
         "langdef/tptp/fof_prenex_normalization_v1.metta"
         TptpFofPrenexNormalize:Result TptpFofPrenexNormalize:Result
         $context-fuel $work-limit (LangDef:RelationRows)
         (tptp-fof-prenex-normalize:request $nnf)))
     (((TPTP:TransformationValueV1
         (tptp-fof-prenex-normalize:result $_prenex-source $prenex)
         $prenex-receipt)
        (tptp:clausify-after-prenex
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $_skolem-environment $_target-depth $_skolem-frontier
            $_naming-depth $_naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $prenex
          $official-receipt $resolver-receipt $normalization-receipt
          $prenex-receipt))
       ((TPTP:TransformationValueV1 $target $_receipt)
        (TPTP:FofClausificationRejectedV1
          MalformedStageTarget fof-prenex-normalization $target))
       ($result $result))))

(= (tptp:clausify-after-prenex
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $skolem-environment $target-depth $skolem-frontier
        $_naming-depth $_naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $prenex
      $official-receipt $resolver-receipt $normalization-receipt
      $prenex-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-skolemization
         "langdef/tptp/fof_skolemization_v1.metta"
         TptpFofSkolemize:FormResult TptpFofSkolemize:FormResult
         $context-fuel $work-limit (LangDef:RelationRows)
         (tptp-fof-skolemize:form-request
           $skolem-environment $target-depth $skolem-frontier $prenex)))
     (((TPTP:TransformationValueV1
         (tptp-fof-skolemize:form-result
           $_environment $_depth $_frontier $_source-formula
           $skolem-form $next-skolem $introduced-skolems)
         $skolem-receipt)
        (tptp:clausify-after-skolem
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $skolem-environment $target-depth $skolem-frontier
            $_naming-depth $_naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $skolem-form
          (tptp-fof-skolem:output
            $skolem-form $next-skolem $introduced-skolems)
          $official-receipt $resolver-receipt $normalization-receipt
          $prenex-receipt $skolem-receipt))
       ((TPTP:TransformationValueV1 $target $_receipt)
        (TPTP:FofClausificationRejectedV1
          MalformedStageTarget fof-skolemization $target))
       ($result $result))))

(= (tptp:clausify-after-skolem
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $skolem-environment $target-depth $skolem-frontier
        $naming-depth $naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $skolem-form $skolem-output
      $official-receipt $resolver-receipt $normalization-receipt
      $prenex-receipt $skolem-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-definitional-naming
         "langdef/tptp/fof_definitional_naming_v1.metta"
         TptpFofName:OpenResult TptpFofName:OpenResult
         $context-fuel $work-limit (LangDef:RelationRows)
         (tptp-fof-name:open-request
           $naming-depth $naming-frontier $skolem-form)))
     (((TPTP:TransformationValueV1
         (tptp-fof-name:open-result
           $_depth $_frontier $_source-formula $named-output)
         $naming-receipt)
        (tptp:clausify-after-naming
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $skolem-environment $target-depth $skolem-frontier
            $naming-depth $naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $skolem-output $named-output
          $official-receipt $resolver-receipt $normalization-receipt
          $prenex-receipt $skolem-receipt $naming-receipt))
       ((TPTP:TransformationValueV1 $target $_receipt)
        (TPTP:FofClausificationRejectedV1
          MalformedStageTarget fof-definitional-naming $target))
       ($result $result))))

(= (tptp:clausify-after-naming
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $skolem-environment $target-depth $skolem-frontier
        $naming-depth $naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $skolem-output $named-output
      $official-receipt $resolver-receipt $normalization-receipt
      $prenex-receipt $skolem-receipt $naming-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-definitional-cnf-generation
         "langdef/tptp/fof_definitional_cnf_generation_v1.metta"
         TptpFofCnfGeneration:GenerateResult
         TptpFofCnfGeneration:GenerateResult
         $context-fuel $work-limit (LangDef:RelationRows)
         (tptp-fof-cnf-gen:generate-request $named-output)))
     (((TPTP:TransformationValueV1
         (tptp-fof-cnf-gen:generate-result $_named $cnf-output)
         $cnf-receipt)
        (tptp:clausify-after-cnf
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $skolem-environment $target-depth $skolem-frontier
            $naming-depth $naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $skolem-output $named-output $cnf-output
          $official-receipt $resolver-receipt $normalization-receipt
          $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt))
       ((TPTP:TransformationValueV1 $target $_receipt)
        (TPTP:FofClausificationRejectedV1
          MalformedStageTarget fof-definitional-cnf-generation $target))
       ($result $result))))

(= (tptp:clausify-after-cnf
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $skolem-environment $target-depth $skolem-frontier
        $naming-depth $naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $skolem-output $named-output $cnf-output
      $official-receipt $resolver-receipt $normalization-receipt
      $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         official-fof-batch-projection
         "langdef/tptp/official_fof_batch_projection_v1.metta"
         TptpFofBatchGen:Request TptpFofBatchGen:Request
         $context-fuel $work-limit (LangDef:RelationRows)
         (tptp-fof-batch-project:request
           $source $skolem-output $cnf-output)))
     (((TPTP:TransformationValueV1 $batch-request $projection-receipt)
        (tptp:clausify-after-projection
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $skolem-environment $target-depth $skolem-frontier
            $naming-depth $naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $skolem-output $named-output $cnf-output
          $batch-request
          $official-receipt $resolver-receipt $normalization-receipt
          $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
          $projection-receipt))
       ($result $result))))

(= (tptp:clausify-after-projection
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $skolem-environment $target-depth $skolem-frontier
        $naming-depth $naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $skolem-output $named-output $cnf-output
      $batch-request
      $official-receipt $resolver-receipt $normalization-receipt
      $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
      $projection-receipt)
   (case $batch-request
     (((tptp-fof-batch-gen:request
         $_projected-occurrence $projected-polarity
         $_projected-skolem $_projected-cnf)
        (case (== $polarity $projected-polarity)
          ((True
             (tptp:clausify-projected-batch
               (TPTP:FofClausificationConfigV1
                 $context-fuel $work-limit $binder-rows
                 $skolem-environment $target-depth $skolem-frontier
                 $naming-depth $naming-frontier $_clause-frontier
                 $_serialization-rows)
               $source $polarity $skolem-output $named-output $cnf-output
               $batch-request
               $official-receipt $resolver-receipt $normalization-receipt
               $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
               $projection-receipt))
            (False
             (TPTP:FofClausificationRejectedV1
               PolarityMismatch $polarity $projected-polarity)))))
       ($_
        (TPTP:FofClausificationRejectedV1
          MalformedStageTarget official-fof-batch-projection
          $batch-request)))))

(= (tptp:clausify-projected-batch
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $skolem-environment $target-depth $skolem-frontier
        $naming-depth $naming-frontier $_clause-frontier
        $_serialization-rows)
      $source $polarity $skolem-output $named-output $cnf-output
      $batch-request
      $official-receipt $resolver-receipt $normalization-receipt
      $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
      $projection-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-clausification-batch-generation
         "langdef/tptp/fof_clausification_batch_generation_v1.metta"
         TptpFofBatchGen:Request TptpFofBatch:Output
         $context-fuel $work-limit (LangDef:RelationRows)
         $batch-request))
     (((TPTP:TransformationValueV1 $batch $batch-receipt)
        (tptp:clausify-after-batch
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $skolem-environment $target-depth $skolem-frontier
            $naming-depth $naming-frontier $_clause-frontier
            $_serialization-rows)
          $source $polarity $skolem-output $named-output $cnf-output $batch
          $official-receipt $resolver-receipt $normalization-receipt
          $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
          $projection-receipt $batch-receipt))
       ($result $result))))

(= (tptp:clausify-after-batch
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $binder-rows
        $skolem-environment $target-depth $skolem-frontier
        $naming-depth $naming-frontier $clause-frontier
        $_serialization-rows)
      $source $polarity $skolem-output $named-output $cnf-output $batch
      $official-receipt $resolver-receipt $normalization-receipt
      $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
      $projection-receipt $batch-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-cnf-name-allocation
         "langdef/tptp/fof_cnf_name_allocation_v1.metta"
         TptpFofCnfNameAllocation:Request TptpFofCnfAllocated:Output
         $context-fuel $work-limit (LangDef:RelationRows)
         (tptp-fof-cnf-name-allocation:request
           $batch $clause-frontier)))
     (((TPTP:TransformationValueV1 $allocated $allocation-receipt)
        (tptp:clausify-after-allocation
          (TPTP:FofClausificationConfigV1
            $context-fuel $work-limit $binder-rows
            $skolem-environment $target-depth $skolem-frontier
            $naming-depth $naming-frontier $clause-frontier
            $_serialization-rows)
          $source $polarity $skolem-output $named-output $cnf-output
          $batch $allocated
          $official-receipt $resolver-receipt $normalization-receipt
          $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
          $projection-receipt $batch-receipt $allocation-receipt))
       ($result $result))))

(= (tptp:clausify-after-allocation
      (TPTP:FofClausificationConfigV1
        $context-fuel $work-limit $_binder-rows
        $_skolem-environment $_target-depth $_skolem-frontier
        $_naming-depth $_naming-frontier $_clause-frontier
        $serialization-rows)
      $source $polarity $skolem-output $named-output $cnf-output
      $batch $allocated
      $official-receipt $resolver-receipt $normalization-receipt
      $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
      $projection-receipt $batch-receipt $allocation-receipt)
   (case
     (tptp:require-single-stage-execution
       (tptp:language-stage-execution
         fof-cnf-official-ast
         "langdef/tptp/fof_cnf_official_ast_v1.metta"
         TptpFofCnfOfficialSerialization:Output
         TptpFofCnfOfficialSerialization:Output
         $context-fuel $work-limit $serialization-rows
         (tptp-cnf-official-serialization:serialize $allocated)))
     (((TPTP:TransformationValueV1 $official-output $serialization-receipt)
        (TPTP:FofClausificationResultV1
          $source $polarity
          $skolem-output $named-output $cnf-output
          $batch $allocated $official-output
          (TPTP:FofClausificationOperationalReceiptsV1
            $official-receipt $resolver-receipt $normalization-receipt
            $prenex-receipt $skolem-receipt $naming-receipt $cnf-receipt
            $projection-receipt $batch-receipt $allocation-receipt
            $serialization-receipt)))
       ($result $result))))

; ------------------------------------------------------ MeTTa artifacts --

; Resolve the LanguageDef wire that owns each persistable semantic stage.
; The mapping is explicit so neither term shape nor a caller-provided path can
; silently change the admission authority of a saved artifact.
(= (tptp:stage-language-wire $stage)
   (case $stage
     ((clause-data
        "langdef/logic/first_order_clause_data_v1.metta")
      (first-order-document
        "langdef/logic/tptp_first_order_document_v1.metta")
      (first-order-derivation
        "langdef/logic/tptp_first_order_derivation_v1.metta")
      (official-derivation
        "langdef/tptp/official_semantic_carrier_v9200.metta")
      ($_ (TPTP:ArtifactRejected UnsupportedStage $stage)))))

(= (tptp:stage-language-wire-sha256 $stage)
   (let $wire (tptp:stage-language-wire $stage)
     (case $wire
       (((TPTP:ArtifactRejected $reason $detail)
          (TPTP:ArtifactRejected $reason $detail))
        ($wire-path
          (let $language (langdef:load-language $wire-path)
            (case $language
              (((NativeHandle $kind $identifier)
                 (let $wire-sha256
                      (langdef:language-wire-sha256 $language)
                   (let $_ (langdef:close-language $language)
                     $wire-sha256)))
               ($fault
                 (TPTP:ArtifactRejected
                   StageLanguageUnavailable $stage $fault))))))))))

; `repr` is CeTTa's sole public MeTTa printer and is an alpha-inverse of
; `parse` on admitted syntax.  A persisted artifact records both its public
; stage and the exact bytes of the LanguageDef wire that owned admission.
(= (tptp:to-metta-text $stage-value)
   (case $stage-value
     (((TPTP:StageValue $stage $_value)
        (case (tptp:stage-language-wire-sha256 $stage)
          (((LangDef:LanguageWireSHA256 $wire-sha256)
             (let $artifact
                  (TPTP:SemanticArtifactV1
                    $stage $wire-sha256 $stage-value)
               (TPTP:MettaText
                 $stage $wire-sha256 (repr $artifact))))
           ((TPTP:ArtifactRejected $reason $detail)
             (TPTP:MettaTextRejected $reason $detail))
           ($fault
             (TPTP:MettaTextRejected
               StageLanguageUnavailable $stage $fault)))))
      ($other
        (TPTP:MettaTextRejected NotStageValue $other)))))

; Parse one MeTTa artifact, require the caller's stage, and reject a payload
; whose recorded LanguageDef wire differs from the wire installed now.  This
; checks provenance only; semantic re-admission remains a separate operation.
(= (tptp:from-metta-text $expected-stage $text)
   (case (collapse (parse $text))
     ((((Error $_source $_message))
        (TPTP:MettaTextRejected ParseFailed $expected-stage))
      (((TPTP:SemanticArtifactV1
          $actual-stage $recorded-sha256
          (TPTP:StageValue $payload-stage $value)))
        (case (== $expected-stage $actual-stage)
          ((True
             (case (== $actual-stage $payload-stage)
               ((True
                  (case
                    (tptp:stage-language-wire-sha256 $actual-stage)
                    (((LangDef:LanguageWireSHA256 $current-sha256)
                       (case (== $recorded-sha256 $current-sha256)
                         ((True
                            (TPTP:StageValue $actual-stage $value))
                          (False
                            (TPTP:MettaTextRejected
                              LanguageRevisionMismatch $actual-stage
                              $recorded-sha256 $current-sha256)))))
                     ((TPTP:ArtifactRejected $reason $detail)
                       (TPTP:MettaTextRejected $reason $detail))
                     ($fault
                       (TPTP:MettaTextRejected
                         StageLanguageUnavailable
                         $actual-stage $fault)))))
                (False
                  (TPTP:MettaTextRejected
                    EnvelopeStageMismatch
                    $actual-stage $payload-stage)))))
           (False
             (TPTP:MettaTextRejected
               StageMismatch $expected-stage $actual-stage)))))
      (($parsed)
        (TPTP:MettaTextRejected
          NotSemanticArtifact $expected-stage $parsed))
      ($observed
        (TPTP:MettaTextRejected
          ParseMultiplicity $expected-stage $observed)))))

; Re-admit a decoded artifact under the LanguageDef that owns its public
; semantic stage.  This is intentionally an explicit dispatcher: a stage is
; never inferred from term shape, and unsupported stages do not acquire an
; accidental validation policy.
(= (tptp:re-admit-stage-value $stage-value)
   (case $stage-value
     (((TPTP:StageValue $stage $value)
        (case $stage
          ((clause-data
             (tptp:admit-stage
               clause-data
               "langdef/logic/first_order_clause_data_v1.metta"
               "Problem" $value))
           (first-order-document
             (tptp:admit-stage
               first-order-document
               "langdef/logic/tptp_first_order_document_v1.metta"
               "Document" $value))
           (first-order-derivation
             (tptp:admit-first-order-derivation $value))
           (official-derivation
             (tptp:admit-official-derivation $value))
           ($_
             (TPTP:ArtifactRejected UnsupportedStage $stage)))))
      ($other
        (TPTP:ArtifactRejected NotStageValue $other)))))

; ------------------------------------------------------------- inspection --

(= (tptp:clause-data-clauses-count (fo-cnf:clauses-nil)) 0)

(= (tptp:clause-data-clauses-count
      (fo-cnf:clauses-cons $first $rest))
   (+ 1 (tptp:clause-data-clauses-count $rest)))

(= (tptp:clause-data-problem-clause-count $problem)
   (let (fo-cnf:problem $source $clauses) $problem
     (tptp:clause-data-clauses-count $clauses)))

(= (tptp:first-order-inputs-count (tptp-fo:inputs-nil)) 0)

(= (tptp:first-order-inputs-count
      (tptp-fo:inputs-cons $first $rest))
   (+ 1 (tptp:first-order-inputs-count $rest)))

(= (tptp:first-order-document-input-count $document)
   (let (tptp-fo:document $source $inputs) $document
     (tptp:first-order-inputs-count $inputs)))

(= (tptp:first-order-derivation-nodes-count
      (tstp:derivation-nodes-nil))
   0)

(= (tptp:first-order-derivation-nodes-count
      (tstp:derivation-nodes-cons $first $rest))
   (+ 1 (tptp:first-order-derivation-nodes-count $rest)))

(= (tptp:first-order-derivation-node-count
      (tstp:derivation $_source $nodes))
   (tptp:first-order-derivation-nodes-count $nodes))

(= (tptp:abstract-syntax-inputs-count
      (tptp92-ast:list:tptp92ast-tptp-input:nil))
   0)

(= (tptp:abstract-syntax-inputs-count
      (tptp92-ast:list:tptp92ast-tptp-input:cons $first $rest))
   (+ 1 (tptp:abstract-syntax-inputs-count $rest)))

(= (tptp:abstract-syntax-input-count
      (TPTP:AbstractSyntax $_source-digest
        (tptp92-ast:tptp-file:alt-1 $inputs)))
   (tptp:abstract-syntax-inputs-count $inputs))
