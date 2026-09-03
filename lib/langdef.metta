; Generic runtime interface for compiled compositional GSLT language definitions.
; A manifest selects only generated artifacts and their digest lock.  All
; language-specific syntax and transformations remain data below langdef/.

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

; Compute every contextual reduct of an exact PApp/FVar Pattern wire.  The
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
; (LangDef:DeterministicCall head argument ...) envelope so the call remains
; data until the checked plan owns its evaluation.
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
        ($_ (return $parsed))))))

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
