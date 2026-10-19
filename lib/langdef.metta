; Generic runtime interface for compiled compositional GSLT language definitions.
; A manifest selects only generated artifacts and their digest lock.  All
; language-specific syntax and transformations remain data below langdef/.

; Syntax values keep the existing LanguageDef and parser-profile carriers.
; ScalarScannerlessAllV1 names the current policy: preserve all scalar parses,
; not maximal munch. SyntaxOriginsV1 carries one LCons/LNil list for categories,
; grammar productions, lexical classes and lexical productions, respectively.
; Each SyntaxOriginV1 has a source namespace and a declaration-site identity.
; These are caller-declared identities, not inferred source attestations.
; Compute operands with let/case before handing them to these data operations.
(: __cetta_lib_syntax_gslt_admit_v1 (-> Atom Atom Atom Atom Atom))
(: __cetta_lib_syntax_gslt_inspect_v1 (-> Atom Atom))
(: __cetta_lib_syntax_gslt_inspect_checked_v1 (-> Atom Atom))
(: __cetta_lib_syntax_gslt_join_v1 (-> Atom Atom Atom Atom Atom))
(: __cetta_lib_syntax_gslt_join_coherent_v1 (-> Atom Atom Atom Atom Atom))
(: langdef:syntax-gslt (-> Atom Atom Atom Atom Atom))
(: langdef:inspect-syntax-gslt (-> Atom Atom))
(: langdef:inspect-syntax-gslt-checked (-> Atom Atom))
(: langdef:join-syntax-gslts (-> Atom Atom Atom Atom Atom))
(: langdef:join-coherent-syntax-gslts (-> Atom Atom Atom Atom Atom))
(: langdef:load-syntax-gslt-parser (-> Atom %Undefined%))

(= (langdef:syntax-gslt $language $profile $policy $origins)
   (function
     (chain (eval (__cetta_lib_syntax_gslt_admit_v1 $language $profile $policy $origins))
       $value (return $value))))

(= (langdef:inspect-syntax-gslt $value)
   (function
     (chain (eval (__cetta_lib_syntax_gslt_inspect_v1 $value))
       $parts (return $parts))))

; Observation does not certify the contents of a data envelope. Request this
; checked operation when inspection must also establish admission.
(= (langdef:inspect-syntax-gslt-checked $value)
   (function
     (chain (eval (__cetta_lib_syntax_gslt_inspect_checked_v1 $value))
       $parts (return $parts))))

; Origin-view join refines the existing Lean module algebra: identical full
; origin/label/body entries share their first position; renamed views at one
; origin remain distinct; unequal entries at one exposed label conflict.
; Category, production and lexical-class names have separate namespaces;
; grammar and lexical production labels share the production namespace.
; The name and start category of the result are chosen explicitly. Declaration
; order is stable within each existing carrier field. No selected-parse
; conservativity certificate follows from a successful structural join.
(= (langdef:join-syntax-gslts $name $start $first $second)
   (function
     (chain (eval (__cetta_lib_syntax_gslt_join_v1 $name $start $first $second))
       $value (return $value))))

; Origin-coherent composition adds agreement of every representative at a
; shared origin; it is deliberately not an alias for origin-view composition.
(= (langdef:join-coherent-syntax-gslts $name $start $first $second)
   (function
     (chain (eval (__cetta_lib_syntax_gslt_join_coherent_v1 $name $start $first $second))
       $value (return $value))))

; Compilation validates the data envelope as well as its grammar and profile;
; ordinary inspection is observation-only. Parser publication uses the
; existing authored-value entry and its owned handle protocol.
(: __cetta_lib_authored_parser_load_syntax (-> Atom %Undefined%))
(: __cetta_lib_parser_compile (-> Atom Atom %Undefined%))
(: langdef:compile-parser (-> Atom Atom %Undefined%))
(= (langdef:compile-parser $syntax $strategy)
   (function
     (case (__cetta_lib_parser_compile $syntax $strategy)
       (((return $handle) (return $handle))
        ($result (return $result))))))

(= (langdef:load-syntax-gslt-parser $value)
   (function
     (case (__cetta_lib_authored_parser_load_syntax $value)
       (((return $handle) (return $handle))
        ($result (return $result))))))

; Compile an authored concrete-syntax LanguageDef and its independently
; authored lexical profile directly into a reusable native parser.  This is
; the source-facing construction boundary: no generated manifest or
; language-specific host callback is involved.
(= (langdef:load-authored-parser $language-path $profile-path)
   (function
     (case (__cetta_lib_authored_parser_load
             $language-path $profile-path)
       (((return $handle) (return $handle))
        ($result (return $result))))))

; Compile an already constructed LanguageDef and parser-profile value through
; the same canonical ingress, typed decoders, ParserPack compiler, and native
; GLL/GLR execution boundary as the file-based operation.
(: __cetta_lib_authored_parser_load_values (-> Atom Atom %Undefined%))
(: langdef:load-authored-parser-values (-> Atom Atom %Undefined%))
(= (langdef:load-authored-parser-values $language $profile)
   (function
     (case (__cetta_lib_authored_parser_load_values $language $profile)
       (((return $handle) (return $handle))
        ($result (return $result))))))

(= (langdef:close-authored-parser $handle)
   (function
     (case (__cetta_lib_authored_parser_close $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:authored-parser-info $handle)
   (function
     (case (__cetta_lib_authored_parser_info $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

(: langdef:parse-authored-text (-> %Undefined% %Undefined% Atom))
(: langdef:parse-authored-file (-> %Undefined% %Undefined% Atom))
(: langdef:parse-authored-text-evidence (-> %Undefined% %Undefined% %Undefined% Atom))
(: langdef:parse-authored-file-evidence (-> %Undefined% %Undefined% %Undefined% Atom))
(= (langdef:parse-authored-text $handle $source)
   (function
     (case (__cetta_lib_authored_parser_parse_text $handle $source)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:parse-authored-file $handle $path)
   (function
     (case (__cetta_lib_authored_parser_parse_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

; Return the complete canonical packed forest as ordinary `pf-v1` MeTTa data
; in addition to replayed semantic values.  The explicit item limit bounds
; each of the root, node, and choice lists; exceeding it is a typed failure,
; never a digest-only substitute.  GLL and GLR must materialize the same term.
(= (langdef:parse-authored-text-evidence $handle $source $item-limit)
   (function
     (case (__cetta_lib_authored_parser_parse_text_evidence
             $handle $source $item-limit)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:parse-authored-file-evidence $handle $path $item-limit)
   (function
     (case (__cetta_lib_authored_parser_parse_file_evidence
             $handle $path $item-limit)
       (((return $result) (return $result))
        ($result (return $result))))))

; Ordinary parsing projects values and source spans without constructing a
; receipt or canonical evidence. Explicit authored evidence calls are separate.
; Result fields are data. Structural observers must not execute the grammar's
; constructors or force computations inside a projected syntax tree.
(: ParserAcceptedV1 (-> Atom Atom Atom))
(: ParserSpanV1 (-> Atom Atom Atom Atom Atom))
(: ParserRejectedV1 (-> Atom Atom Atom Atom))
(: ParserLexicalRejectedV1 (-> Atom Atom Atom Atom))
(: ParserIncompleteV1 (-> Atom Atom Atom Atom))
(: ParserCyclicV1 (-> Atom Atom))
(: ParserFailureV1 (-> Atom Atom))
(: ParserInfoV1 (-> Atom Atom Atom Atom Atom Atom Atom))
(: __cetta_lib_parser_parse_text (-> %Undefined% String Atom))
(: __cetta_lib_parser_parse_file (-> %Undefined% String Atom))
(: __cetta_lib_parser_info (-> %Undefined% Atom))
(: langdef:parser-parse-text (-> %Undefined% %Undefined% Atom))
(: __cetta_lib_parser_parse_text_forest (-> %Undefined% String %Undefined% Atom))
(: langdef:parser-parse-text-forest (-> %Undefined% %Undefined% %Undefined% Atom))
(: ParserSelectionV1 (-> Atom Atom))
(: ParserRejectFollowingClassV1 (-> Atom Atom Atom))
(: ParserSelectionObligationV1 (-> Atom Atom Atom))
(: __cetta_lib_parser_parse_text_forest_selected (-> %Undefined% String Atom %Undefined% Atom))
(: langdef:parser-parse-text-forest-selected (-> %Undefined% %Undefined% Atom %Undefined% Atom))
(: __cetta_lib_parser_parse_text_selected (-> %Undefined% String Atom Atom))
(: langdef:parser-parse-text-selected (-> %Undefined% %Undefined% Atom Atom))
(: langdef:parser-parse-file (-> %Undefined% %Undefined% Atom))
(: langdef:parser-info (-> %Undefined% Atom))
(= (langdef:parser-parse-text $handle $text)
   (function
     (chain (eval (__cetta_lib_parser_parse_text $handle $text))
       $result (return $result))))
; Retain the existing complete packed forest alongside the ordinary result.
; This is evidence generation, not the separate GLL/GLR agreement audit.
(= (langdef:parser-parse-text-forest $handle $text $item-limit)
   (function
     (chain (eval (__cetta_lib_parser_parse_text_forest $handle $text $item-limit))
       $result (return $result))))
; Select derivation families before expanding their semantic child products.
; The returned packed forest remains the complete, unselected parse evidence.
(= (langdef:parser-parse-text-forest-selected $handle $text $selection $item-limit)
   (function
     (chain (eval (__cetta_lib_parser_parse_text_forest_selected $handle $text $selection $item-limit))
       $result (return $result))))
; The same selected observation without serializing successful parse evidence.
; Unresolved selections and grammar diagnostics retain their original forest.
(= (langdef:parser-parse-text-selected $handle $text $selection)
   (function
     (chain (eval (__cetta_lib_parser_parse_text_selected $handle $text $selection))
       $result (return $result))))
(= (langdef:parser-parse-file $handle $path)
   (function
     (chain (eval (__cetta_lib_parser_parse_file $handle $path))
       $result (return $result))))
(= (langdef:parser-info $handle)
   (function
     (chain (eval (__cetta_lib_parser_info $handle))
       $result (return $result))))
(= (langdef:close-parser $handle)
   (function
     (case (__cetta_lib_authored_parser_close $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

; Freeze owns construction work; ordinary loading restores immutable programs.
; Checked loading additionally verifies integrity and the stored structure.
(: __cetta_lib_parser_freeze (-> %Undefined% String Atom))
(: __cetta_lib_parser_load_artifact (-> String Atom))
(: __cetta_lib_parser_load_artifact_checked (-> String Atom))
(: __cetta_lib_parser_load_baked (-> String Atom))
(: langdef:freeze-parser (-> %Undefined% %Undefined% Atom))
(: langdef:load-parser-artifact (-> %Undefined% Atom))
(: langdef:load-parser-artifact-checked (-> %Undefined% Atom))
(: langdef:load-baked-parser (-> %Undefined% Atom))
(= (langdef:freeze-parser $handle $path)
   (function
     (chain (eval (__cetta_lib_parser_freeze $handle $path))
       $result (return $result))))
(= (langdef:load-parser-artifact $path)
   (function
     (chain (eval (__cetta_lib_parser_load_artifact $path))
       $result (return $result))))
(= (langdef:load-parser-artifact-checked $path)
   (function
     (chain (eval (__cetta_lib_parser_load_artifact_checked $path))
       $result (return $result))))
(= (langdef:load-baked-parser $name)
   (function
     (chain (eval (__cetta_lib_parser_load_baked $name))
       $result (return $result))))

(= (langdef:load $manifest)
   (function
     (case (__cetta_lib_langdef_load $manifest)
       (((return $handle) (return $handle))
        ($result (return $result))))))

(= (langdef:close $handle)
   (function
     (case (__cetta_lib_langdef_close $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

; Load one checked LanguageDef wire as a reusable ground-term admission
; authority.  This handle does not parse source text and does not install any
; language-specific evaluator; it only owns the decoded constructor profile.
(= (langdef:load-language $wire-path)
   (function
     (case (__cetta_lib_language_def_term_load $wire-path)
       (((return $handle) (return $handle))
        ($result (return $result))))))

(= (langdef:close-language $handle)
   (function
     (case (__cetta_lib_language_def_term_close $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

(: __cetta_lib_language_def_term_load_value (-> Atom %Undefined%))
(: langdef:load-language-value (-> Atom %Undefined%))
(= (langdef:load-language-value $wire)
   (function
     (case (__cetta_lib_language_def_term_load_value $wire)
       (((return $handle) (return $handle))
        ($result (return $result))))))

; Exact content identity of the checked LanguageDef wire owned by a live
; admission handle.  The digest covers the wire bytes that were decoded; it
; is suitable for rejecting artifacts produced against a different revision.
(= (langdef:language-wire-sha256 $handle)
   (function
     (case (__cetta_lib_language_def_term_wire_sha256 $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

; Exact admission of a ground term at a named category.  Rejection is data,
; preserving the decoder's stable status and diagnostic rather than turning a
; malformed transformation result into an installed atom.
(= (langdef:admit-term $handle $category $term)
   (function
     (case (__cetta_lib_language_def_term_admit
             $handle $category $term)
       (((return $result) (return $result))
        ($result (return $result))))))

; Project every labelled parser alternative into the selected LanguageDef's
; ground carrier, retaining its occurrence source map. Non-accepted parser
; outcomes pass through; a malformed alternative refuses the whole batch.
(: langdef:project-parser-result (-> %Undefined% %Undefined% Atom Atom))
(: __cetta_lib_language_def_project_parser_result (-> %Undefined% %Undefined% Atom %Undefined%))
(: LangDef:ProjectedTermsV1 (-> Atom Atom Atom))
(: LangDef:ProjectedTermV1 (-> Atom Atom Atom))
(: LangDef:TermSourceV1 (-> Atom Atom Atom Atom Atom Atom))
(= (langdef:project-parser-result $handle $category $result)
   (function
     (case (__cetta_lib_language_def_project_parser_result $handle $category $result)
       (((return $value) (return $value))
        ($value (return $value))))))

; Named-token binding is an explicit projection profile, not a convention
; imposed on every grammar. It shares the alternative and source-map carrier.
(: langdef:project-parser-pattern-result (-> %Undefined% %Undefined% Atom Atom Atom))
(: __cetta_lib_language_def_project_parser_pattern_result (-> %Undefined% %Undefined% Atom Atom %Undefined%))
(: LangDef:NamedTokensV1 (-> Atom Atom))
(: LangDef:BinderSourceV1 (-> Atom Atom Atom Atom Atom))
(= (langdef:project-parser-pattern-result $handle $category $profile $result)
   (function
     (case (__cetta_lib_language_def_project_parser_pattern_result $handle $category $profile $result)
       (((return $value) (return $value))
        ($value (return $value))))))

; Select one already projected alternative explicitly, returning object data
; without evaluating its constructors in the host language.
(: langdef:projected-term (-> Atom %Undefined% Atom))
(: __cetta_lib_language_def_projected_term (-> Atom %Undefined% %Undefined%))
(= (langdef:projected-term $projection $index)
   (function
     (case (__cetta_lib_language_def_projected_term $projection $index)
       (((return $term) (return $term))
        ($term (return $term))))))

; Select one contextual reduct as inert object data. The original result
; retains its completeness flag, ordered alternatives and derivation traces.
(: langdef:contextual-result-term (-> Atom %Undefined% Atom))
(: __cetta_lib_language_def_contextual_result_term (-> Atom %Undefined% %Undefined%))
(= (langdef:contextual-result-term $results $index)
   (function
     (case (__cetta_lib_language_def_contextual_result_term $results $index)
       (((return $term) (return $term))
        ($term (return $term))))))

; Compute every contextual reduct in the supported Pattern profile. The
; result preserves source-rule order, multiplicity, nested congruence evidence,
; and explicit relation-provider receipts.  This low-level semantic boundary
; deliberately does not coerce ordinary guest atoms into Pattern values.
(= (langdef:rewrite-pattern-at
      $handle $context-fuel $work-limit $relation-rows $pattern)
   (function
     (case (__cetta_lib_language_def_rewrite_pattern_at
             $handle $context-fuel $work-limit $relation-rows $pattern)
       (((return $result) (return $result))
        ($result (return $result))))))

; Typed convenience boundary over the same contextual relation.  The source
; guest term is admitted at source-category, every reduct is admitted at
; target-category, and the ordered result list retains multiplicity and proof
; traces.  Relation rows remain exact Pattern data because their schemas are
; declared by the relation provider rather than by one guest-term category.
(= (langdef:rewrite-term-at
      $handle $source-category $target-category
      $context-fuel $work-limit $relation-rows $term)
   (function
     (case (__cetta_lib_language_def_rewrite_term_at
             $handle $source-category $target-category
             $context-fuel $work-limit $relation-rows $term)
       (((return $result) (return $result))
        ($result (return $result))))))

; The data variants use the same admission and contextual engines, but keep
; term, Pattern and relation-row operands inert under host equations. Compute
; those values with let/case before this handoff; use the operations above
; when operands are programs that should be interpreted first.
; Closed locally nameless Pattern admission uses the same core declarations.
; Display names are metadata; bound indices are checked in their typed scope.
(: langdef:admit-pattern-data (-> %Undefined% %Undefined% Atom Atom))
(: LangDef:TermIncompleteV1 (-> Atom Atom Atom))
(: __cetta_lib_language_def_pattern_admit_data (-> %Undefined% %Undefined% Atom %Undefined%))
(= (langdef:admit-pattern-data $handle $category $pattern)
   (function
     (case (__cetta_lib_language_def_pattern_admit_data $handle $category $pattern)
       (((return $result) (return $result))
        ($result (return $result))))))

(: langdef:admit-term-data (-> %Undefined% %Undefined% Atom Atom))
(: __cetta_lib_language_def_term_admit_data (-> %Undefined% %Undefined% Atom %Undefined%))
(= (langdef:admit-term-data $handle $category $term)
   (function
     (case (__cetta_lib_language_def_term_admit_data $handle $category $term)
       (((return $result) (return $result))
        ($result (return $result))))))

(: langdef:rewrite-pattern-at-data (-> %Undefined% %Undefined% %Undefined% Atom Atom Atom))
(: __cetta_lib_language_def_rewrite_pattern_at_data (-> %Undefined% %Undefined% %Undefined% Atom Atom %Undefined%))
(= (langdef:rewrite-pattern-at-data $handle $context-fuel $work-limit $relation-rows $pattern)
   (function
     (case (__cetta_lib_language_def_rewrite_pattern_at_data
             $handle $context-fuel $work-limit $relation-rows $pattern)
       (((return $result) (return $result))
        ($result (return $result))))))

(: langdef:rewrite-term-at-data (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined% Atom Atom Atom))
(: __cetta_lib_language_def_rewrite_term_at_data (-> %Undefined% %Undefined% %Undefined% %Undefined% %Undefined% Atom Atom %Undefined%))
(= (langdef:rewrite-term-at-data $handle $source-category $target-category
      $context-fuel $work-limit $relation-rows $term)
   (function
     (case (__cetta_lib_language_def_rewrite_term_at_data
             $handle $source-category $target-category
             $context-fuel $work-limit $relation-rows $term)
       (((return $result) (return $result))
        ($result (return $result))))))

; Compile a bounded structural tree relabeler from the selected equations of
; a source LanguageDef.  The compiled plan contains no guest-language
; constructor table other than the constructors read from those equations.
(= (langdef:load-structural-tree-relabel
      $language-path $entry-operator $label-operator)
   (function
     (case (__cetta_lib_structural_tree_relabel_load
             $language-path $entry-operator $label-operator)
       (((return $handle) (return $handle))
        ($result (return $result))))))

; Compile the same relabeler from an ordered gslt-composition-v1.  The native
; plan reads every named source; the composition is therefore the runtime
; authority rather than a provenance-only companion to a flattened table.
(= (langdef:load-structural-tree-relabel-composition
      $composition-path $entry-operator $label-operator)
   (function
     (case (__cetta_lib_structural_tree_relabel_load_composition
             $composition-path $entry-operator $label-operator)
       (((return $handle) (return $handle))
        ($result (return $result))))))

(= (langdef:structural-tree-relabel $handle $source)
   (function
     (case (__cetta_lib_structural_tree_relabel_apply $handle $source)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:close-structural-tree-relabel $handle)
   (function
     (case (__cetta_lib_structural_tree_relabel_close $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

; Compile an exact deterministic first-order evaluator from one or more
; source equation LanguageDefs.  Sources are supplied in a
; (LangDef:EquationSources ...) envelope; execution uses a
; (LangDef:DeterministicCall head argument ...) envelope. Ordinary run permits
; argument computations; run-data applies the plan to existing values.
(: __cetta_lib_deterministic_equations_run_data (-> Atom Atom Atom))
; The named-binding natives take terms as data and give data back.
(: __cetta_abt_free_names (-> Atom Atom Atom))
(: __cetta_abt_substitute (-> Atom Atom Atom Atom))
(: __cetta_abt_rename_bound (-> Atom Atom Atom Atom))
(: __cetta_abt_named_alpha_eq (-> Atom Atom Atom %Undefined%))
(: __cetta_abt_instantiate (-> Atom Atom Atom Atom))
; Reify raw syntax as code without running it or forming Prime lambdas
; inside it. Existing scope records and variable identities are preserved.
; This differs from lifting an already formed Prime value into code.
(: __cetta_abt_quote_syntax (-> Atom Atom))
(: langdef:quote-syntax (-> Atom %Undefined%))

(= (langdef:load-deterministic-equations $sources)
   (function
     (case (__cetta_lib_deterministic_equations_load $sources)
       (((return $handle) (return $handle))
        ($result (return $result))))))

(= (langdef:run-deterministic-equations $handle $call)
   (function
     (case (__cetta_lib_deterministic_equations_run $handle $call)
       (((return $result) (return $result))
        ($result (return $result))))))

; Apply an authored transformation to already computed data arguments.
; Neither argument subtrees nor the returned value are implicitly executed.
; Logical variable atoms in supplied data retain their identities; they are
; distinct from the authored equations' metavariables. A Prime target is
; formed explicitly with prime:elaborate before using it as executable code.
(= (langdef:run-deterministic-equations-data $handle $call)
   (function
     (case (__cetta_lib_deterministic_equations_run_data $handle $call)
       (((return $result) (return $result))
        ($result (return $result))))))

; Test-scoped: the same deterministic run with an explicit work allowance,
; so oracle comparisons can witness cases beyond the fixed production budget.
(= (langdef:run-deterministic-equations-allowance $handle $call $allowance)
   (function
     (case (__cetta_lib_deterministic_equations_run_allowance $handle $call $allowance)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:close-deterministic-equations $handle)
   (function
     (case (__cetta_lib_deterministic_equations_close $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:info $handle)
   (function
     (case (__cetta_lib_langdef_info $handle)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:parse-text $handle $source)
   (function
     (case (__cetta_lib_langdef_parse_text $handle $source)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:parse-file $handle $path)
   (function
     (case (__cetta_lib_langdef_parse_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

; Recognize one complete source with the checked parser, without constructing
; a canonical forest or replaying semantic actions.  Acceptance cardinality is
; one; it is not a parse-tree or semantic-result count.  Clients that need
; values must use parse-file, prepare-import-file, or import-file explicitly.
(= (langdef:recognize-file $handle $path)
   (function
     (case (__cetta_lib_langdef_recognize_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

; Bounded recognition for a top-level language that is a concatenable sequence
; of records.  The delimiter byte proposes cut points but does not decide them:
; a prefix is committed only when the complete ParserPack accepts it and its
; forest contains a nonempty record-state span.  The caller must establish
; that concatenating such accepted prefixes preserves its top-level language.
(= (langdef:recognize-delimited-sequence-file
      $handle $record-state $delimiter-byte
      $candidate-limit $buffer-limit $path)
   (function
     (case
       (__cetta_lib_langdef_recognize_delimited_sequence_file
         $handle $record-state $delimiter-byte
         $candidate-limit $buffer-limit $path)
       (((return $result) (return $result))
        ($result (return $result))))))

; Parse a source graph into immutable constructor data for a generated target
; program.  Includes are expanded by the declared source-control plan; no
; source occurrence is installed as executable target code.
(= (langdef:parse-occurrence-stream-file $handle $path)
   (function
     (case (__cetta_lib_langdef_parse_occurrence_stream_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:prepare-import-file $handle $path)
   (function
     (case (__cetta_lib_langdef_prepare_import_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:import-identity-file $handle $path)
   (function
     (case (__cetta_lib_langdef_import_identity_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

; The native syntax-import interface is retained as an explicit diagnostic seam.
; Language users normally call langdef:import-file, whose generated entry
; transforms syntax before anything is published.
(= (langdef:import-syntax-file $handle $path)
   (function
     (case (__cetta_lib_langdef_import_syntax_file $handle $path)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:import-entry $handle)
   (function
     (case (__cetta_lib_langdef_import_entry $handle)
       (((return $entry) (return $entry))
        ($result (return $result))))))

(= (langdef:publish $handle $protocol)
   (function
     (case (__cetta_lib_langdef_publish $handle $protocol)
       (((return (LangDef:PublishResult $result)) (return $result))
        ((return (LangDef:PublishFailure $source $message))
         (return (Error $source $message)))
        ($result (return $result))))))

; Apply a manifest-owned import entry through an ordinary equation boundary.
; The entry remains data until this rule binds it as the callable head.
(= (langdef:invoke-entry $entry $prepared)
   ($entry $prepared))

(= (langdef:import-dispatch
      (LangDef:ImportIdentity) $handle $path)
   (langdef:import-identity-file $handle $path))

(= (langdef:import-dispatch
      (LangDef:ImportEntry $entry) $handle $path)
   (let $parsed (langdef:prepare-import-file $handle $path)
     (case $parsed
       (((LangDef.ImportPrepared $name $pack $source $values)
         (let $protocol (langdef:invoke-entry $entry $parsed)
           (langdef:publish $handle $protocol)))
        ($_ $parsed)))))

; Recognition and generated semantic replay finish before the target space
; changes. Rejection leaves the target untouched; acceptance publishes the
; generated constructs as one transaction.
(= (langdef:import-file $handle $path)
   (let $entry (langdef:import-entry $handle)
     (langdef:import-dispatch $entry $handle $path)))

; Generic projection from SyntaxCoreV1's cp/pair/cons carrier to one symbol.
; Language stages use this instead of reimplementing UTF-8 assembly.
(= (langdef:codepoints->symbol $codepoints)
   (function
     (case (__cetta_lib_langdef_codepoints_symbol $codepoints)
       (((return $result) (return $result))
        ($result (return $result))))))

; Generic projection from SyntaxCoreV1's cp/pair/cons carrier to one grounded
; UTF-8 string.  Unlike symbol projection, the empty sequence is valid.
(= (langdef:codepoints->string $codepoints)
   (function
     (case (__cetta_lib_langdef_codepoints_string $codepoints)
       (((return $result) (return $result))
        ($result (return $result))))))

; Inverse of codepoints->symbol for generic source-language algorithms.
(= (langdef:symbol->codepoints $symbol)
   (function
     (case (__cetta_lib_langdef_symbol_codepoints $symbol)
       (((return $result) (return $result))
        ($result (return $result))))))

(= (langdef:append-symbol $left $right)
   (function
     (case (__cetta_lib_langdef_symbol_append $left $right)
       (((return $result) (return $result))
        ($result (return $result))))))

; Named binding over an ABT signature whose binders declare names that occur
; in the term (BindTelescope, BindPattern): the free variable names in order
; of first occurrence, simultaneous capture-avoiding substitution of
; ((NAME VALUE) ...), renaming binders apart from each other, from the free
; names and from avoid, and equality up to the names of binders.  Terms are
; data: nothing in them is evaluated.
(= (langdef:free-names $signature $term)
   (__cetta_abt_free_names $signature $term))
(= (langdef:substitute-names $signature $bindings $term)
   (__cetta_abt_substitute $signature $bindings $term))
(= (langdef:rename-apart $signature $avoid $term)
   (__cetta_abt_rename_bound $signature $avoid $term))
(= (langdef:alpha-equal $signature $left $right)
   (__cetta_abt_named_alpha_eq $signature $left $right))

; Export the same declared scope analysis with an explicit ambient frame,
; newest name first. Bound references become idx nodes; free names stay
; unresolved. The result is sealed syntax, with no implicit execution.
(= (langdef:resolve-context $signature $context $term)
   (function
     (case (__cetta_abt_contextual_canonical $signature $context $term)
       (($result (return $result))))))

; Instantiate the local names of declared syntax. Variables uses private
; identities, as `new` does; nominal name classes use freshness relative to
; the term and avoid list. Free dependencies remain shared. The returned
; AbtInstance retains the new term and one AbtBinding per binder occurrence.
; This operation acts on data; execution still belongs to the language's
; declared interpreter or lowering. Quoted source is opened explicitly.
(= (langdef:instantiate-names $signature $avoid $term)
   (__cetta_abt_instantiate $signature $avoid $term))

; Syntax held as code: the argument is data, and it is never run.
(= (langdef:quote-syntax $syntax)
   (__cetta_abt_quote_syntax $syntax))

; A code instance carries the chosen signature, sealed source, instantiated
; syntax, and complete freshening receipts. Syntax and receipts are sealed
; code fields, so inspecting the packet cannot execute a hidden command or
; fill a retained name. No agenda is installed globally.
(= (langdef:instantiate-code $signature $avoid $code)
   (let (quote $source) $code
     (case (langdef:instantiate-names $signature $avoid $source)
       (((AbtInstance $term $receipts)
         (let $held-term (langdef:quote-syntax $term)
           (let $held-receipts (langdef:quote-syntax $receipts)
             (LangDef:BindingInstance
               $signature $code $held-term $held-receipts))))
        ($failure $failure)))))

(= (langdef:instance-code
      (LangDef:BindingInstance $signature $source $code $receipts))
   $code)

(= (langdef:instance-bindings
      (LangDef:BindingInstance $signature $source $term $receipts))
   $receipts)

; An authored lowering is selected per use. Its result retains both its
; interpreter and the source instance, so consumers may inspect the original
; scope declaration and freshening receipts. This is a computation receipt,
; not a proof of the lowering's semantic or typing preservation.
(= (langdef:lower-instance $handle $entry $instance)
   (let (quote $term) (langdef:instance-code $instance)
     (let $target (langdef:run-deterministic-equations-data $handle
                    (LangDef:DeterministicCall $entry $term))
       (let $code (langdef:quote-syntax $target)
         (LangDef:BindingLowering $handle $entry $instance $code)))))

(= (langdef:lowered-code
      (LangDef:BindingLowering $handle $entry $instance $code))
   $code)

(= (langdef:lowering-source
      (LangDef:BindingLowering $handle $entry $instance $target))
   $instance)
