; Public TPTP library.
;
; The library runs in the HE and the PeTTa lane, under the base and the
; extended profile alike.  This file is the shared source, spelled in base
; HE; its PeTTa spelling is generated beside it under lib/petta, and the
; loader picks the spelling of the running lane.  Compact values are list
; values and byte strings, which every lane handles as values; only their
; written literals need the extended profile.
;
; The default all-family reader loads a pinned ParserPack snapshot derived
; from the official Extended-BNF source and returns the canonical term of each
; TPTP input in the language derived from that grammar: every alternative is a
; constructor named by its rule (and alternative index, where a rule has
; several), whose arguments are its value children in order; a token is named
; by its token rule with its text; fixed tokens and layout are dropped,
; single-child alternatives are transparent, and repetitions and separated
; lists are list values, each the child of a node named by its list rule.
; Every constructor application and token is written (bnf NAME child...), so
; grammar names such as include never stand where an evaluator could reduce
; them.  For example
;   fof(q,axiom,![X,Y]:p(X,Y)).
; reads as
;   (bnf fof_annotated (bnf lower_word "q") (bnf lower_word "axiom")
;     (bnf fof_quantified_formula "!"
;       (bnf fof_variable_list [(bnf upper_word "X") (bnf upper_word "Y")])
;       (bnf fof_plain_term (bnf lower_word "p")
;         (bnf fof_arguments [(bnf upper_word "X") (bnf upper_word "Y")])))
;     (bnf nothing))
; The compact TPTP form is an explicit interpretation of these terms: a file
; is the list of its records, and every list in them is a list value, so
;   fof(q,axiom,![X,Y]:p(X,Y)).
; is [(fof q axiom (∀ [X Y] (p X Y)))] under the mixed policy.
;
; Semantic stage operations below use the generic LanguageDef runtime.
;
; TPTP specifies logic and interchange formats, not a proof-search strategy.
; Accordingly this library contains no prover.  ATP examples may consume the
; admitted semantic values below, while TSTP verification will remain a
; separate, explicit operation when its semantic derivation layer is present.

!(import! &self langdef)
!(import! &self fs)
!(import! &self list)
!(import! &self str)

; Pinned official SyntaxBNF digest. A mismatch is a named fault, not a parse.
(= (tptp:official-syntaxbnf-digest)
   "f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6")
(= (tptp:official-reader-cache-path)
   "runtime/tptp-official-reader/f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6/pack.sexpr")
(= (tptp:official-snapshot-path)
   "runtime/tptp-official-reader/f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6/tables.tpp1")
(= (tptp:corpus-compatible-snapshot-path)
   "runtime/tptp-official-reader/f47940c43c23ed5ed8633a3b74a2847648d5ab794669430c8f0c38f138e61df6/corpus-compatible/tables.tpp1")
(= (tptp:snapshot-construct $pack $out)
   (function
     (case (__cetta_lib_tptp_snapshot_construct_v1 $pack $out)
       (((return $ok) (return $ok))
        ($result (return $result))))))
; A corpus-compatible pack is constructed against the strict snapshot of the
; same SyntaxBNF; the productions its extensions add are the ones a reading
; avoids.
(= (tptp:snapshot-construct-compatible $pack $strict $out)
   (function
     (case (__cetta_lib_tptp_snapshot_construct_v1 $pack $strict $out)
       (((return $ok) (return $ok))
        ($result (return $result))))))
(= (tptp:snapshot-info $path)
   (function
     (case (__cetta_lib_tptp_snapshot_info_v1 $path)
       (((return $ok) (return $ok))
        ($result (return $result))))))
(= (tptp:write-atom $path $atom)
   (function
     (case (__cetta_lib_tptp_write_atom_v1 $path $atom)
       (((return $ok) (return $ok))
        ($result (return $result))))))
(= (tptp:read-atom $path)
   (function
     (case (__cetta_lib_tptp_read_atom_v1 $path)
       (((return $atom) (return $atom))
        ($result (return $result))))))
(= (tptp:syntaxbnf-digest $path)
   (function
     (case (__cetta_lib_tptp_syntaxbnf_digest_v1 $path)
       (((return $digest) (return $digest))
        ($result (return $result))))))

; A reusable reader owns one validated pinned snapshot selected by an explicit
; profile. Its lifetime follows the arena that owns the handle; callers do not
; need a matching close. The ordinary read operations below select strict.
(= (tptp:open-reader $profile)
   (function
     (case (__cetta_lib_tptp_reader_open_v1 $profile)
       (((return $reader) (return $reader))
        ($result (return $result))))))
(= (tptp:reader-info $reader)
   (function
     (case (__cetta_lib_tptp_reader_info_v1 $reader)
       (((return $info) (return $info))
        ($result (return $result))))))
; Text of a canonical term occupying a position of the named grammar symbol,
; printed by the rows of the derived LanguageDef.
(= (tptp:print-canonical $reader $symbol $term)
   (let $handle $reader
     (let $rule $symbol
       (function
         (case (__cetta_lib_tptp_reader_canonical_print_v1 $handle $rule $term)
           (((return $text) (return $text))
            ($result (return $result))))))))
; The LanguageDef of the reader's canonical terms, derived from the grammar.
(= (tptp:canonical-language $reader)
   (function
     (case (__cetta_lib_tptp_reader_canonical_language_v1 $reader)
       (((return $language) (return $language))
        ($result (return $result))))))
(= (tptp:reader-artifact-info $reader)
   (function
     (case (__cetta_lib_tptp_reader_artifact_info_v1 $reader)
       (((return $info) (return $info))
        ($result (return $result))))))

; Default public read: the compact records of a file or text, from the pinned
; snapshot of the strict profile, under the mixed leaf policy.  A reader of
; either profile and the policy (tagged, mixed or native) may be given.  The
; compact form is the checked interpretation of the canonical terms in
; langdef/tptp/canonical_to_compact_v1.metta and the profile's companion file;
; reading returns it as data and evaluates nothing.
(= (tptp:read-file $path)
   (case (tptp:open-reader strict)
     (((NativeHandle $kind $id)
        (tptp:read-file (NativeHandle $kind $id) mixed $path))
      ($fault $fault))))
(= (tptp:read-file $reader $path)
   (tptp:read-file $reader mixed $path))
(= (tptp:read-file $reader $policy $path)
   (function
     (case (__cetta_lib_tptp_reader_read_compact_file_v1 $reader $policy $path)
       (((return $records) (return $records))
        ($result (return $result))))))
(= (tptp:read-text $text)
   (case (tptp:open-reader strict)
     (((NativeHandle $kind $id)
        (tptp:read-text (NativeHandle $kind $id) mixed $text))
      ($fault $fault))))
(= (tptp:read-text $reader $text)
   (tptp:read-text $reader mixed $text))
(= (tptp:read-text $reader $policy $text)
   (function
     (case (__cetta_lib_tptp_reader_read_compact_text_v1 $reader $policy $text)
       (((return $records) (return $records))
        ($result (return $result))))))
(= (tptp:read-input $text)
   (tptp:read-text $text))

; TPTP text of compact records of any policy, one input per line.  Printing
; writes each value's own form back: the printer adds the parentheses the
; grammar needs, so text reads back to the same compact values.
(= (tptp:print $records)
   (case (tptp:open-reader strict)
     (((NativeHandle $kind $id)
        (tptp:print (NativeHandle $kind $id) $records))
      ($fault $fault))))
(= (tptp:print $reader $records)
   (let $handle $reader
     (function
       (case (__cetta_lib_tptp_reader_print_compact_v1 $handle (quote $records))
         (((return $text) (return $text))
          ($result (return $result)))))))

; Import adds records to a space the caller names, in file order and one atom
; per occurrence, so a duplicated input is two atoms, and returns the records
; as data.  Nothing in them is evaluated.
(= (tptp:import $space $records)
   (let $_ (tptp-internal:add-records $space $records) $records))
(= (tptp:import-file $space $path)
   (let $records (tptp:read-file $path) (tptp:import $space $records)))
(= (tptp:import-file $space $reader $policy $path)
   (let $records (tptp:read-file $reader $policy $path)
     (tptp:import $space $records)))
(= (tptp:import-text $space $text)
   (let $records (tptp:read-text $text) (tptp:import $space $records)))
(= (tptp:import-text $space $reader $policy $text)
   (let $records (tptp:read-text $reader $policy $text)
     (tptp:import $space $records)))

(= (tptp-internal:add-records $space $records)
   (if (== (size-atom $records) 0)
       $records
       (let $head (car-atom $records)
         (let $rest (cdr-atom $records)
           (let $_ (add-atom $space $head)
             (tptp-internal:add-records $space $rest))))))

; Evaluation is a separate act the caller chooses: each record, in order, is
; evaluated with the equations of the given space, and the result lists, per
; record, all of its results.
(= (tptp:evaluate $space $records)
   (if (== (size-atom $records) 0)
       $records
       (let $head (car-atom $records)
         (let $rest (cdr-atom $records)
           (let $results (collapse (evalc $head $space))
             (let $tail (tptp:evaluate $space $rest)
               (cons-atom $results $tail)))))))

; Binding.  The compact form declares its binders to CeTTa's ABT layer, and
; the operations below are langdef's named-binding operations over that
; declaration.  A quantifier's list declares its variables one after another,
; each in scope in the types of the declarations after it and in the body.
; $let declares its constants in the body and in the left sides of its
; definitions; a definition's left side binds its variables in its right
; side, which is outside the $let.  Variables are (var "X") under the tagged
; policy and the symbols of TPTP's upper-case spelling under the mixed
; policy, including ?V for a host-literal spelling; the native policy has no
; ?V.
(= (tptp:binding-signature $policy)
   (let $names (tptp:variable-names $policy)
     (let $let (tptp:let-head $policy)
       (AbtSignatures
         $names
         (AbtSignature ∀ (Fields variables body)
           (BindTelescope variables (Scope body) (Annotated : 1 2)))
         (AbtSignature ∃ (Fields variables body)
           (BindTelescope variables (Scope body) (Annotated : 1 2)))
         (AbtSignature λ (Fields variables body)
           (BindTelescope variables (Scope body) (Annotated : 1 2)))
         (AbtSignature ε (Fields variables body)
           (BindTelescope variables (Scope body) (Annotated : 1 2)))
         (AbtSignature ι (Fields variables body)
           (BindTelescope variables (Scope body) (Annotated : 1 2)))
         (AbtSignature Π (Fields variables body)
           (BindTelescope variables (Scope body) (Annotated : 1 2)))
         (AbtSignature Σ (Fields variables body)
           (BindTelescope variables (Scope body) (Annotated : 1 2)))
         (AbtSignature (Head (defined "#")) (Fields variables body)
           (BindTelescope variables (Scope body) (Annotated : 1 2)))
         (AbtSignature $let (Fields typings definitions body)
           (BindTelescope typings (Scope body (Each definitions 1))
             (Annotated : 1 2)))
         (AbtSignature ≔ (Fields left right)
           (BindPattern left (Scope right)))))))
(= (tptp:variable-names tagged) (AbtNames (Form var)))
(= (tptp:variable-names mixed)
   (AbtNames (SymbolInitial "A" "Z") (SymbolPrefix "?")))
(= (tptp:variable-names native) (AbtNames (SymbolInitial "A" "Z")))
(= (tptp:let-head tagged) (Head (defined "let")))
(= (tptp:let-head mixed) §let)
(= (tptp:let-head native) §let)

; The binding operations take formulas as data (their formula parameters are
; Atoms) and give a computed formula back inside quote, the one carrier that
; crosses an evaluating position on every host; a fault comes back as it is.
; The free variables of a formula or any other compact value, each once, in
; order of first occurrence, as a list: (∀ VARS F) closes F over them.
(: tptp:free-variables (-> Atom %Undefined%))
(: tptp:free-variables (-> Atom Atom %Undefined%))
(= (tptp:free-variables $value)
   (tptp:free-variables mixed $value))
(= (tptp:free-variables $policy $value)
   (let $signature (tptp:binding-signature $policy)
     (let $names (__cetta_abt_free_names $signature $value)
       (__cetta_lib_list_from_expression $names))))

; Replace free variables simultaneously: ((X t) (Y u)) replaces X by t and Y
; by u.  A binder is renamed only where it would capture a variable of a
; replacement, to its name followed by a number that occurs nowhere else.
; The result is (TPTP:Value FORMULA): a computed value is handed back inside
; a carrier whose field is data, the same on both hosts, and is taken apart
; with let or unify.
(: tptp:substitute (-> Atom Atom %Undefined%))
(: tptp:substitute (-> Atom Atom Atom %Undefined%))
(= (tptp:substitute $bindings $value)
   (tptp:substitute mixed $bindings $value))
(= (tptp:substitute $policy $bindings $value)
   (let $signature (tptp:binding-signature $policy)
     (let $result (__cetta_abt_substitute $signature $bindings $value)
       (tptp-internal:quoted $result))))

; Rename bound variables apart: every binder then declares a name no other
; binder declares, that is not free in the value, and that is not in avoid.
; The result is (TPTP:Value FORMULA).
(: tptp:rename-apart (-> Atom Atom %Undefined%))
(: tptp:rename-apart (-> Atom Atom Atom %Undefined%))
(= (tptp:rename-apart $avoid $value)
   (tptp:rename-apart mixed $avoid $value))
(= (tptp:rename-apart $policy $avoid $value)
   (let $signature (tptp:binding-signature $policy)
     (let $result (__cetta_abt_rename_bound $signature $avoid $value)
       (tptp-internal:quoted $result))))

; Equality up to the names of bound variables.
(: tptp:alpha-equal (-> Atom Atom %Undefined%))
(: tptp:alpha-equal (-> Atom Atom Atom %Undefined%))
(= (tptp:alpha-equal $left $right)
   (tptp:alpha-equal mixed $left $right))
(= (tptp:alpha-equal $policy $left $right)
   (let $signature (tptp:binding-signature $policy)
     (__cetta_abt_named_alpha_eq $signature $left $right)))

; A computed value inside its carrier; a fault as it is.
(: TPTP:Value (-> Atom Atom))
(: tptp-internal:quoted (-> Atom %Undefined%))
(= (tptp-internal:quoted $value)
   (unify $value (Error $call $reason) $value (TPTP:Value $value)))

; The canonical terms themselves, (bnf NAME child...), for programs that work
; with the grammar's own structure.
(= (tptp:read-canonical-file $path)
   (case (tptp:open-reader strict)
     (((NativeHandle $kind $id)
        (tptp:read-canonical-file (NativeHandle $kind $id) $path))
      ($fault $fault))))
(= (tptp:read-canonical-file $reader $path)
   (function
     (case (__cetta_lib_tptp_reader_read_file_v1 $reader $path)
       (((return $records) (return $records))
        ($result (return $result))))))
(= (tptp:read-canonical-text $text)
   (case (tptp:open-reader strict)
     (((NativeHandle $kind $id)
        (tptp:read-canonical-text (NativeHandle $kind $id) $text))
      ($fault $fault))))
(= (tptp:read-canonical-text $reader $text)
   (function
     (case (__cetta_lib_tptp_reader_read_text_v1 $reader $text)
       (((return $records) (return $records))
        ($result (return $result))))))

; A source document keeps the exact original text beside its records and
; the span of each record's input, (TPTP:Span START STOP) in bytes.  The spans
; partition the text: an input runs from its first token, or the start of the
; text, to the next input's first token, or the end of the text.
(= (tptp:read-text-document $text)
   (case (tptp:open-reader strict)
     (((NativeHandle $kind $id)
        (tptp:read-text-document (NativeHandle $kind $id) mixed $text))
      ($fault $fault))))
(= (tptp:read-text-document $reader $text)
   (tptp:read-text-document $reader mixed $text))
(= (tptp:read-text-document $reader $policy $text)
   (tptp-internal:document (TPTP:TextSource) $reader $policy $text))
(= (tptp:read-file-document $path)
   (case (tptp:open-reader strict)
     (((NativeHandle $kind $id)
        (tptp:read-file-document (NativeHandle $kind $id) mixed $path))
      ($fault $fault))))
(= (tptp:read-file-document $reader $path)
   (tptp:read-file-document $reader mixed $path))
(= (tptp:read-file-document $reader $policy $path)
   (case (fs:read-text $path)
     (((Error $source $message) (Error $source $message))
      ($text
        (tptp-internal:document (TPTP:FileSource $path) $reader $policy $text)))))
(= (tptp-internal:document $identity $reader $policy $text)
   (let $handle $reader
     (let $read (function
                  (case (__cetta_lib_tptp_reader_read_compact_document_v1
                          $handle $policy $text)
                    (((return $value) (return $value))
                     ($value (return $value)))))
       (case $read
         (((TPTP:ReadDocument $records $spans)
            (let $digest (str:sha256 $text)
              (TPTP:SourceDocument $identity $digest $text $records $spans)))
          ($fault $fault))))))
(= (tptp:source-document-identity
      (TPTP:SourceDocument $identity $_digest $_text $_records $_spans))
   $identity)
(= (tptp:source-document-digest
      (TPTP:SourceDocument $_identity $digest $_text $_records $_spans))
   $digest)
(= (tptp:source-document-text
      (TPTP:SourceDocument $_identity $_digest $text $_records $_spans))
   $text)
(= (tptp:source-document-records
      (TPTP:SourceDocument $_identity $_digest $_text $records $_spans))
   $records)
(= (tptp:source-document-spans
      (TPTP:SourceDocument $_identity $_digest $_text $_records $spans))
   $spans)
(= (tptp:print-source-document $document)
   (case $document
     (((TPTP:SourceDocument $_identity $_digest $text $_records $_spans) $text)
      ($_ (TPTP:UnsupportedSourceDocument $document)))))

; Parsed records retain exact lexical spellings.  This authored view exposes
; the semantic string used by file lookup and name comparison.
(= (tptp:open-compact-lexical-values)
   (langdef:load-deterministic-equations
     (LangDef:EquationSources
       "langdef/bnf/plain_bnf_denotation_v1.metta"
       "langdef/tptp/official_compact_lexical_value_v1.metta")))
(= (tptp:compact-lexical-value-with $program $operation $value)
   (langdef:run-deterministic-equations $program
     (LangDef:DeterministicCall $operation $value)))
(= (tptp:compact-lexical-value $operation $value)
   (let $program (tptp:open-compact-lexical-values)
     (case $program
       (((NativeHandle $_kind $_id)
          (let $answers
               (collapse
                 (tptp:compact-lexical-value-with
                   $program $operation $value))
            (let $_ (langdef:close-deterministic-equations $program)
              (superpose $answers))))
        ($fault (TPTP:LexicalProgramFailure $fault))))))
(= (tptp:word-value $word)
   (tptp:compact-lexical-value tptp-lex-v1:word-value $word))
(= (tptp:name-value $name)
   (tptp:compact-lexical-value tptp-lex-v1:name-value $name))

; The first-order resolution input of a document's clauses: each clause keeps
; its source occurrence, name and role, with its variables scoped by that
; occurrence, admitted against the resolution input language.  The document
; is read under the tagged policy; a formula must be clausified first and an
; include expanded, and either is refused by name.
(= (tptp:open-resolution-input-view)
   (langdef:load-deterministic-equations
     (LangDef:EquationSources
       "langdef/tptp/compact_to_resolution_input_v1.metta")))
(= (tptp:source-document->resolution-input $document)
   (case $document
     (((TPTP:SourceDocument $_identity $_digest $_text $_records $_spans)
        (let $program (tptp:open-resolution-input-view)
          (case $program
            (((NativeHandle $_kind $_id)
               (let $answers
                    (collapse
                      (tptp:source-document->resolution-input-with
                        $program $document))
                 (let $_ (langdef:close-deterministic-equations $program)
                   (superpose $answers))))
             ($fault (TPTP:ResolutionInputProgramFailureV1 $fault))))))
      ($other $other))))
; The same over a view program the caller loaded and closes.
(= (tptp:source-document->resolution-input-with $program $document)
   (tptp:resolution-input-run-with $program
     (LangDef:DeterministicCall tptp-res-v1:problem $document)))
(= (tptp:resolution-input-run-with $program $call)
   (let $value (langdef:run-deterministic-equations $program $call)
     (case $value
       (((TPTP:ResolutionInputUnsupportedV1 $family) $value)
        ($_ (tptp:admit-or-propagate resolution-input
              "langdef/logic/first_order_resolution_input_v1.metta"
              "Problem" $value))))))
; The resolution input of a file with its includes expanded, every file read
; under the tagged policy.  Each clause of each file reached is an input whose
; occurrence names its own file's digest and index and the include directive
; that reached that file, so a file included twice gives two inputs with
; separate variables.  The problem's source is the root file's digest.
(= (tptp:read-file-expanded-resolution-input $root-path $library-root)
   (tptp:read-file-expanded-resolution-input
     (tptp:open-reader strict) $root-path $library-root))
(= (tptp:read-file-expanded-resolution-input $reader $root-path $library-root)
   (tptp:include-environment->resolution-input
     (tptp:load-compact-include-environment-with-reader
       $reader tagged $root-path $library-root)))
; The same over an include environment the caller loaded, which keeps the
; source documents that justify each input available to a checker.
(= (tptp:include-environment->resolution-input $loaded)
   (case $loaded
     (((TPTP:CompactIncludeEnvironmentV1 $root-source $_library-root $environment)
        (case (tptp:include-environment-document $root-source $environment)
          (((TPTP:IncludeEnvironmentDocumentV1 $_source $digest $_records)
             (tptp:expanded-document->resolution-input $digest
               (tptp:resolve-compact-include-environment
                 $root-source $environment)))
           ($missing $missing))))
      ($fault $fault))))
; The document an include environment loaded from a source path.
(= (tptp:include-environment-document $source
      (tptp-ci-v1:environment $documents $_bindings))
   (tptp:include-documents-find $source $documents))
(= (tptp:include-documents-find $source (tptp-ci-v1:documents-nil))
   (TPTP:IncludeEnvironmentDocumentMissingV1 $source))
(= (tptp:include-documents-find $source
      (tptp-ci-v1:documents-cons
        (tptp-ci-v1:source-document $candidate $digest $records $_text)
        $rest))
   (if (== $candidate $source)
       (TPTP:IncludeEnvironmentDocumentV1 $candidate $digest $records)
       (tptp:include-documents-find $source $rest)))
(= (tptp:expanded-document->resolution-input $digest $expanded)
   (case $expanded
     (((TPTP:ExpandedDocumentV1 $_root $_inputs $_edges)
        (let $program (tptp:open-resolution-input-view)
          (case $program
            (((NativeHandle $_kind $_id)
               (let $answers
                    (collapse
                      (tptp:resolution-input-run-with $program
                        (LangDef:DeterministicCall
                          tptp-res-v1:expanded-problem $digest $expanded)))
                 (let $_ (langdef:close-deterministic-equations $program)
                   (superpose $answers))))
             ($fault (TPTP:ResolutionInputProgramFailureV1 $fault))))))
      ($other $other))))
; One compact record as the resolution clause at a given occurrence: the
; projection that a provenance check recomputes, over a view program the
; caller loaded and closes.  A record that is not a clause is refused by name.
(= (tptp:record->resolution-clause-with $program $occurrence $record)
   (langdef:run-deterministic-equations $program
     (LangDef:DeterministicCall tptp-res-v1:record-clause $occurrence $record)))

; ------------------------------------------------ formula clausification --
;
; A formula reaches the resolution input through the verified clausification
; stages: binder resolution, normalization of the formula or, for a
; conjecture, of its negation, prenexing, Skolemization, definitional naming
; and clause generation.  Every stage must give exactly one complete result.
; The Skolem and naming frontiers are explicit and threaded: each formula
; starts where the previous one stopped, so no introduced symbol is shared by
; two formulas.  The clausification of each formula is recorded with its
; occurrence, polarity and frontiers, so that a checker can recompute it.
(= (tptp:clausification-context-fuel) 1024)
(= (tptp:clausification-work-limit) 200000000)

(= (tptp:clausification-stage $stage $wire $category $rows $request)
   (tptp:require-single-stage-execution
     (tptp:language-stage-execution
       $stage $wire $category $category
       (tptp:clausification-context-fuel) (tptp:clausification-work-limit)
       $rows $request)))

; Binder resolution consumes one explicit equality decision per ordered pair
; of the formula's bound names.
(= (tptp:binder-equality-rows $names)
   (let $rows (tptp:binder-equality-rows-outer $names $names 1 ())
     (cons-atom LangDef:RelationRows $rows)))
(= (tptp:binder-equality-rows-outer LNil $all $receipt $rows) $rows)
(= (tptp:binder-equality-rows-outer (LCons $name $rest) $all $receipt $rows)
   (let (TPTP:BinderRowsV1 $next $extended)
        (tptp:binder-equality-rows-inner $name $all $receipt $rows)
     (tptp:binder-equality-rows-outer $rest $all $next $extended)))
(= (tptp:binder-equality-rows-inner $name LNil $receipt $rows)
   (TPTP:BinderRowsV1 $receipt $rows))
(= (tptp:binder-equality-rows-inner $name (LCons $other $rest) $receipt $rows)
   (let $decision
        (if (== $name $other)
            "pattern-equality-decision:equal"
            "pattern-equality-decision:different")
     (let $row
          (LangDef:RelationRow "PatternEqualityDecision" $receipt
            (PApp $name LNil) (PApp $other LNil) (PApp $decision LNil))
       (let $extended (cons-atom $row $rows)
         (tptp:binder-equality-rows-inner $name $rest (+ $receipt 1) $extended)))))

(= (tptp:clausification-normalization $polarity $resolved)
   (case $polarity
     (((tptp-fof-batch:positive)
        (tptp:clausification-stage fof-positive-normalization
          "langdef/tptp/fof_normalization_v1.metta" NNFFormula
          (LangDef:RelationRows) (tptp-fof-normalize:positive $resolved)))
      ((tptp-fof-batch:negative)
        (tptp:clausification-stage fof-negative-normalization
          "langdef/tptp/fof_normalization_v1.metta" NNFFormula
          (LangDef:RelationRows) (tptp-fof-normalize:negative $resolved))))))

; The verified stages from a named formula to its clausal form.
(= (tptp:clausify-named-formula
      $occurrence $polarity $named $rows $skolem-frontier $naming-frontier)
   (case (tptp:clausification-stage named-fof-to-resolved
           "langdef/tptp/named_fof_to_resolved_v1.metta" TptpResolvedFof:Formula
           $rows (tptp-fof-resolve:closed $named))
     (((TPTP:TransformationValueV1 $resolved $_resolution-receipt)
        (case (tptp:clausification-normalization $polarity $resolved)
          (((TPTP:TransformationValueV1 $nnf $_normalization-receipt)
             (tptp:clausify-normal-form
               $occurrence $polarity $nnf $skolem-frontier $naming-frontier))
           ($fault $fault))))
      ($fault $fault))))

(= (tptp:clausify-normal-form
      $occurrence $polarity $nnf $skolem-frontier $naming-frontier)
   (case (tptp:clausification-stage fof-prenex-normalization
           "langdef/tptp/fof_prenex_normalization_v1.metta"
           TptpFofPrenexNormalize:Result (LangDef:RelationRows)
           (tptp-fof-prenex-normalize:request $nnf))
     (((TPTP:TransformationValueV1
         (tptp-fof-prenex-normalize:result $_source $prenex) $_prenex-receipt)
        (case (tptp:clausification-stage fof-skolemization
                "langdef/tptp/fof_skolemization_v1.metta"
                TptpFofSkolemize:FormResult (LangDef:RelationRows)
                (tptp-fof-skolemize:form-request
                  (tptp-fof-skolem-term:env-nil) (tptp-fof-resolved:index-zero)
                  $skolem-frontier $prenex))
          (((TPTP:TransformationValueV1
              (tptp-fof-skolemize:form-result
                $_environment $_depth $_frontier $_source-formula
                $skolem-form $next-skolem $introduced-skolems)
              $_skolem-receipt)
             (tptp:clausify-skolem-form
               $occurrence $polarity $skolem-form $skolem-frontier $next-skolem
               $introduced-skolems $naming-frontier))
           ($fault $fault))))
      ($fault $fault))))

(= (tptp:clausify-skolem-form
      $occurrence $polarity $skolem-form $skolem-frontier $next-skolem
      $introduced-skolems $naming-frontier)
   (case (tptp:clausification-stage fof-definitional-naming
           "langdef/tptp/fof_definitional_naming_v1.metta"
           TptpFofName:OpenResult (LangDef:RelationRows)
           (tptp-fof-name:open-request
             (tptp-fof-resolved:index-zero) $naming-frontier $skolem-form))
     (((TPTP:TransformationValueV1
         (tptp-fof-name:open-result $_depth $_frontier $_source $named-output)
         $_naming-receipt)
        (case (tptp:clausification-stage fof-definitional-cnf-generation
                "langdef/tptp/fof_definitional_cnf_generation_v1.metta"
                TptpFofCnfGeneration:GenerateResult (LangDef:RelationRows)
                (tptp-fof-cnf-gen:generate-request $named-output))
          (((TPTP:TransformationValueV1
              (tptp-fof-cnf-gen:generate-result $_named
                (tptp-fof-cnf:output
                  (tptp-fof-named:output
                    $_root $next-naming $_definitions $introduced-definitions)
                  $clauses))
              $_cnf-receipt)
             (TPTP:ClausifiedFormulaV1
               (TPTP:ClausificationV1 $occurrence $polarity
                 $skolem-frontier $next-skolem $introduced-skolems
                 $naming-frontier $next-naming $introduced-definitions)
               $clauses))
           ($fault $fault))))
      ($fault $fault))))

; One formula record at an occurrence, from the given frontiers, as its
; recorded clausification and its resolution clauses.
(= (tptp:clausify-formula-with
      $program $occurrence $record $skolem-frontier $naming-frontier)
   (case (langdef:run-deterministic-equations $program
           (LangDef:DeterministicCall tptp-res-v1:clausification-request $record))
     (((TPTP:ClausificationRequestV1 $polarity $named $names)
        (case (tptp:clausify-named-formula
                $occurrence $polarity $named (tptp:binder-equality-rows $names)
                $skolem-frontier $naming-frontier)
          (((TPTP:ClausifiedFormulaV1 $clausification $clauses)
             (let (TPTP:NameAndRole $_family $name $role) (tptp:record-name-and-role $record)
               (case (langdef:run-deterministic-equations $program
                       (LangDef:DeterministicCall tptp-res-v1:clausal-form
                         $occurrence $name $role $polarity $clauses))
                 (((tptp-res-v1:ok $resolution-clauses)
                    (TPTP:ClausifiedClausesV1 $clausification $resolution-clauses))
                  ($refusal $refusal)))))
           ($fault $fault))))
      ($refusal $refusal))))

(: TPTP:NameAndRole (-> Atom Atom Atom Atom))
(: tptp:record-name-and-role (-> Atom %Undefined%))
(= (tptp:record-name-and-role $record)
   (let $shape (tptp-internal:annotated $record)
     (unify $shape (TPTP:Annotated $family $name $role $formula $annotations)
       (TPTP:NameAndRole $family $name $role)
       $shape)))

; The resolution input of inputs with occurrences: a clause projects
; directly, a formula is clausified, anything else is refused by name.
(= (tptp:clausified-items $program $items $skolem $naming $clauses $clausifications)
   (case $items
     ((LNil
        (TPTP:ClausifiedItemsV1 $clauses $clausifications))
      ((LCons (tptp-res-v1:item $occurrence $record) $rest)
        (tptp:clausified-item $program $occurrence $record $rest
          $skolem $naming $clauses $clausifications)))))

(= (tptp:clausified-item
      $program $occurrence $record $rest $skolem $naming $clauses $clausifications)
   (let $family (car-atom $record)
     (if (== $family cnf)
         (case (tptp:record->resolution-clause-with $program $occurrence $record)
           (((fo-resolution:clause $o $n $r $l)
              (let $extended (cons-atom (fo-resolution:clause $o $n $r $l) $clauses)
                (tptp:clausified-items $program $rest $skolem $naming
                  $extended $clausifications)))
            ($refusal $refusal)))
         (if (== $family fof)
             (case (tptp:clausify-formula-with
                     $program $occurrence $record $skolem $naming)
               (((TPTP:ClausifiedClausesV1
                   (TPTP:ClausificationV1 $o $polarity $skolem-in $skolem-out
                     $introduced-skolems $naming-in $naming-out $introduced-definitions)
                   $resolution-clauses)
                  (let $extended
                       (tptp:prepend-resolution-clauses $resolution-clauses $clauses)
                    (let $recorded
                         (cons-atom
                           (TPTP:ClausificationV1 $o $polarity $skolem-in $skolem-out
                             $introduced-skolems $naming-in $naming-out
                             $introduced-definitions)
                           $clausifications)
                      (tptp:clausified-items $program $rest $skolem-out $naming-out
                        $extended $recorded))))
                ($refusal $refusal)))
             (TPTP:ResolutionInputUnsupportedV1 $family)))))

; Resolution clauses onto a newest-first host list.
(= (tptp:prepend-resolution-clauses (fo-resolution:clauses-nil) $acc) $acc)
(= (tptp:prepend-resolution-clauses (fo-resolution:clauses-cons $clause $rest) $acc)
   (let $extended (cons-atom $clause $acc)
     (tptp:prepend-resolution-clauses $rest $extended)))

(= (tptp:resolution-clauses-of-reversed $items $acc)
   (if (== (size-atom $items) 0)
       $acc
       (let ($first $rest) (decons-atom $items)
         (tptp:resolution-clauses-of-reversed $rest
           (fo-resolution:clauses-cons $first $acc)))))

(= (tptp:reverse-host-list $items $acc)
   (if (== (size-atom $items) 0)
       $acc
       (let ($first $rest) (decons-atom $items)
         (let $extended (cons-atom $first $acc)
           (tptp:reverse-host-list $rest $extended)))))

; The problem of inputs with occurrences, admitted against the resolution
; input language, with the clausification of each formula in input order.
(= (tptp:clausified-problem-with $program $digest $items)
   (case (tptp:clausified-items $program $items
           (tptp-fof-resolved:index-zero) (tptp-fof-resolved:index-zero) () ())
     (((TPTP:ClausifiedItemsV1 $reversed $recorded)
        (let $problem
             (fo-resolution:problem (fo-resolution:source-digest $digest)
               (tptp:resolution-clauses-of-reversed $reversed (fo-resolution:clauses-nil)))
          (case (tptp:admit-or-propagate resolution-input
                  "langdef/logic/first_order_resolution_input_v1.metta"
                  "Problem" $problem)
            (((TPTP:StageValue resolution-input $admitted)
               (TPTP:ClausifiedResolutionInputV1 $admitted
                 (tptp:reverse-host-list $recorded ())))
             ($fault $fault)))))
      ($refusal $refusal))))

(= (tptp:clausified-problem $digest $items-call)
   (let $program (tptp:open-resolution-input-view)
     (case $program
       (((NativeHandle $_kind $_id)
          (let $items (langdef:run-deterministic-equations $program $items-call)
            (let $outcome (tptp:clausified-problem-with $program $digest $items)
              (let $_ (langdef:close-deterministic-equations $program)
                $outcome))))
        ($fault (TPTP:ResolutionInputProgramFailureV1 $fault))))))

; The clausified resolution input of a document, and of an include
; environment with its includes expanded.
(= (tptp:source-document->clausified-resolution-input $document)
   (case $document
     (((TPTP:SourceDocument $_identity $digest $_text $records $_spans)
        (tptp:clausified-problem $digest
          (LangDef:DeterministicCall tptp-res-v1:document-items $records $digest 0)))
      ($other $other))))
(= (tptp:include-environment->clausified-resolution-input $loaded)
   (case $loaded
     (((TPTP:CompactIncludeEnvironmentV1 $root-source $_library-root $environment)
        (case (tptp:include-environment-document $root-source $environment)
          (((TPTP:IncludeEnvironmentDocumentV1 $_source $digest $_records)
             (case (tptp:resolve-compact-include-environment $root-source $environment)
               (((TPTP:ExpandedDocumentV1 $_root $inputs $_edges)
                  (tptp:clausified-problem $digest
                    (LangDef:DeterministicCall tptp-res-v1:expanded-items $inputs)))
                ($fault $fault))))
           ($missing $missing))))
      ($fault $fault))))
(= (tptp:read-text-clausified-resolution-input $text)
   (tptp:source-document->clausified-resolution-input
     (tptp:read-text-document (tptp:open-reader strict) tagged $text)))
(= (tptp:read-file-clausified-resolution-input $root-path $library-root)
   (tptp:include-environment->clausified-resolution-input
     (tptp:load-compact-include-environment-with-reader
       (tptp:open-reader strict) tagged $root-path $library-root)))
(= (tptp:read-file-resolution-input $path)
   (tptp:read-file-resolution-input (tptp:open-reader strict) $path))
(= (tptp:read-file-resolution-input $reader $path)
   (tptp:source-document->resolution-input
     (tptp:read-file-document $reader tagged $path)))
(= (tptp:read-text-resolution-input $text)
   (tptp:read-text-resolution-input (tptp:open-reader strict) $text))
(= (tptp:read-text-resolution-input $reader $text)
   (tptp:source-document->resolution-input
     (tptp:read-text-document $reader tagged $text)))

; Include expansion is an authored transformation over the compact public
; records.  Physical path lookup and file reading build the finite environment
; separately; this operation is pure and exposes every edge and fault.
(= (tptp:open-compact-include-resolution)
   (langdef:load-deterministic-equations
     (LangDef:EquationSources
       "langdef/bnf/plain_bnf_denotation_v1.metta"
       "langdef/tptp/official_compact_lexical_value_v1.metta"
       "langdef/tptp/official_compact_include_resolution_v1.metta")))
(= (tptp:resolve-compact-include-environment-with
      $program $root-source $environment)
   (langdef:run-deterministic-equations-data $program
     (LangDef:DeterministicCall
       tptp-ci-v1:resolve $root-source $environment)))
(= (tptp:compact-include-return $answers)
   (case $answers
     ((($result) $result)
      ($_ (TPTP:IncludeResolutionCardinalityFaultV1 $answers)))))
(= (tptp:resolve-compact-include-environment $root-source $environment)
   (let $program (tptp:open-compact-include-resolution)
     (case $program
       (((NativeHandle $_kind $_id)
          (let $answers
               (collapse
                 (tptp:resolve-compact-include-environment-with
                   $program $root-source $environment))
            (let $_ (langdef:close-deterministic-equations $program)
              (tptp:compact-include-return $answers))))
        ($fault (TPTP:IncludeResolutionProgramFailureV1 $fault))))))

; A checked certificate as compact records: the derivation in TSTP form.
; Each input clause becomes (cnf NAME ROLE CLAUSE (file PATH NAME)), each
; step (cnf c_N plain RESOLVENT (inference resolution [(status thm)]
; [LEFT RIGHT])), the last concluding $false; tptp:print prints the records
; and the printed text reads back to the same records.  The files carrier
; pairs every source digest with its path; a document or a loaded include
; environment gives one.  Checking is the caller's: the trace checker and
; the provenance check accept the certificate first.
(= (tptp:open-certificate-records)
   (langdef:load-deterministic-equations
     (LangDef:EquationSources
       "langdef/bnf/plain_bnf_denotation_v1.metta"
       "langdef/tptp/canonical_to_compact_v1.metta"
       "langdef/tptp/certificate_to_compact_records_v1.metta")))
(: tptp:certificate-records (-> Atom Atom Atom %Undefined%))
(= (tptp:certificate-records $problem $certificate $files)
   (let $program (tptp:open-certificate-records)
     (case $program
       (((NativeHandle $_kind $_id)
          (let $answers
               (collapse
                 (langdef:run-deterministic-equations-data $program
                   (LangDef:DeterministicCall
                     tptp-cert-v1:records $problem $certificate $files)))
            (let $_ (langdef:close-deterministic-equations $program)
              (superpose $answers))))
        ($fault (TPTP:CertificateRecordsProgramFailureV1 $fault))))))
(: tptp:certificate-files-of-document (-> Atom %Undefined%))
(= (tptp:certificate-files-of-document $document)
   (unify $document (TPTP:SourceDocument (TPTP:FileSource $path) $digest $text $records $spans)
     (tptp-cert-v1:files-cons (tptp-cert-v1:file $digest $path) (tptp-cert-v1:files-nil))
     (TPTP:UnsupportedSourceDocument $document)))
; A loaded include environment names the root by the path the caller gave
; and every included file by the spelling of the include that reached it.
(: tptp:certificate-files-of-environment (-> Atom Atom %Undefined%))
(= (tptp:certificate-files-of-environment $loaded $root-name)
   (unify $loaded
     (TPTP:CompactIncludeEnvironmentV1 $root $library (tptp-ci-v1:environment $documents $bindings))
     (let $root-digest (tptp-internal:certificate-digest-of $root $documents)
       (tptp-cert-v1:files-cons (tptp-cert-v1:file $root-digest $root-name)
         (tptp-internal:certificate-files-of-bindings $bindings $documents)))
     $loaded))
(= (tptp-internal:certificate-files-of-bindings (tptp-ci-v1:bindings-nil) $documents)
   (tptp-cert-v1:files-nil))
(= (tptp-internal:certificate-files-of-bindings
      (tptp-ci-v1:bindings-cons (tptp-ci-v1:include-binding $source $requested $target $mode) $rest)
      $documents)
   (let $digest (tptp-internal:certificate-digest-of $target $documents)
     (tptp-cert-v1:files-cons (tptp-cert-v1:file $digest $requested)
       (tptp-internal:certificate-files-of-bindings $rest $documents))))
(= (tptp-internal:certificate-digest-of $source (tptp-ci-v1:documents-nil))
   (TPTP:IncludeEnvironmentDocumentMissingV1 $source))
(= (tptp-internal:certificate-digest-of $source
      (tptp-ci-v1:documents-cons (tptp-ci-v1:source-document $candidate $digest $records $text) $rest))
   (if (== $candidate $source)
       $digest
       (tptp-internal:certificate-digest-of $source $rest)))

; Canonical printing is an executed authored transformation.  It normalizes
; layout while retaining the compact record structure on reparsing.
(= (tptp:print-records $records)
   (let $program
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/tptp/official_records_to_text_v1.metta"))
     (case $program
       (((NativeHandle $_kind $_id)
          (let $answers
               (collapse
                 (langdef:run-deterministic-equations-data $program
                   (LangDef:DeterministicCall
                     tptp-print-v1:document $records)))
            (let $_ (langdef:close-deterministic-equations $program)
              (superpose $answers))))
        ($fault (TPTP:PrintProgramFailure $fault))))))

; Occurrence identity is the source digest plus the zero-based input index,
; with the input's span.  The view preserves source order and duplicates and
; reuses each record value.
(= (tptp:record-occurrences $document)
   (case $document
     (((TPTP:SourceDocument $_identity $digest $_text $records $spans)
        (tptp:record-occurrences-records $digest $records $spans 0))
      ($_ (TPTP:UnsupportedSourceDocument $document)))))
(= (tptp:record-occurrences-records $digest $records $spans $index)
   (if (== (size-atom $records) 0)
       LNil
       (let ($head $tail) (decons-atom $records)
         (let ($span $spans-tail) (decons-atom $spans)
           (let $rest
                (tptp:record-occurrences-records
                  $digest $tail $spans-tail (+ $index 1))
             (LCons
               (TPTP:RecordOccurrence (TPTP:Occurrence $digest $index) $span $head)
               $rest))))))
(= (tptp:official-reader $syntax-path)
   (case (== (tptp:syntaxbnf-digest $syntax-path) (tptp:official-syntaxbnf-digest))
     ((True (tptp:open-reader strict))
      (False (TPTP:DigestMismatch (tptp:syntaxbnf-digest $syntax-path))))))

; ------------------------------------------------------ record utilities --

; A parsed file is the list of its records.  No second list or file carrier is
; introduced at the public boundary.
(= (tptp:records $records) $records)
(= (tptp:file-span $_records) (TPTP:SpanUnavailable compact-value))

; Record views.  An annotated input is (FAMILY NAME ROLE FORMULA), with the
; source and then the useful info appended when present, for the families
; thf tff tcf fof cnf tpi; an include is (incl FILE SELECTION? SPACE?).
; A record or a formula is data on both hosts: every function here takes it
; as an Atom, takes it apart with unify, and hands it back inside a carrier
; whose fields are Atoms, so an equation the program has for a record's or a
; formula's head never fires on it.
(: TPTP:RecordName (-> Atom Atom))
(: TPTP:RecordRole (-> Atom Atom))
(: TPTP:RecordFormula (-> Atom Atom))
(: TPTP:RecordAnnotation (-> Atom Atom))
(: TPTP:RecordAnnotation (-> Atom Atom Atom))
(: TPTP:RecordSource (-> Atom Atom))
(: TPTP:RecordInfo (-> Atom Atom))
(: TPTP:UnsupportedRecord (-> Atom Atom))
(: TPTP:Annotated (-> Atom Atom Atom Atom Atom Atom))

(= (tptp-internal:annotated-family $head)
   (case $head
     ((fof True) (cnf True) (tff True) (thf True) (tcf True) (tpi True)
      ($_ False))))

(: tptp-internal:include-record (-> Atom %Undefined%))
(= (tptp-internal:include-record $record)
   (unify $record (incl $file) True
     (unify $record (incl $file $selection) True
       (unify $record (incl $file $selection $space) True False))))

; An annotated record taken apart: (TPTP:Annotated FAMILY NAME ROLE FORMULA
; ANNOTATIONS), ANNOTATIONS the expression of the source and the useful info
; that are present, (), (SOURCE) or (SOURCE INFO); (TPTP:UnsupportedRecord
; RECORD) for any other value.  No list is written here: a list value is a
; profile's syntax, and this library reads in every base profile.
(: tptp-internal:annotated (-> Atom %Undefined%))
(= (tptp-internal:annotated $record)
   (unify $record ($family $name $role $formula)
     (tptp-internal:annotated-of $record
       (TPTP:Annotated $family $name $role $formula ()))
     (unify $record ($family $name $role $formula $source)
       (tptp-internal:annotated-of $record
         (TPTP:Annotated $family $name $role $formula ($source)))
       (unify $record ($family $name $role $formula $source $info)
         (tptp-internal:annotated-of $record
           (TPTP:Annotated $family $name $role $formula ($source $info)))
         (TPTP:UnsupportedRecord $record)))))
(: tptp-internal:annotated-of (-> Atom Atom %Undefined%))
(= (tptp-internal:annotated-of $record $shape)
   (unify $shape (TPTP:Annotated $family $name $role $formula $annotations)
     (if (tptp-internal:annotated-family $family)
         $shape
         (TPTP:UnsupportedRecord $record))
     (TPTP:UnsupportedRecord $record)))

(: tptp:record-family (-> Atom %Undefined%))
(= (tptp:record-family $record)
   (if (tptp-internal:include-record $record)
       include
       (let $shape (tptp-internal:annotated $record)
         (unify $shape (TPTP:Annotated $family $name $role $formula $annotations)
           $family
           $shape))))

(: tptp-internal:record-field (-> Atom Atom Atom Atom %Undefined%))
(= (tptp-internal:record-field $record $kind $wrap $index)
   (if (tptp-internal:include-record $record)
       (TPTP:NotApplicable $kind include)
       (let $shape (tptp-internal:annotated $record)
         (unify $shape (TPTP:Annotated $family $name $role $formula $annotations)
           (unify $index 0
             ($wrap $name)
             (unify $index 1 ($wrap $role) ($wrap $formula)))
           $shape))))

(: tptp:record-name (-> Atom %Undefined%))
(= (tptp:record-name $record)
   (tptp-internal:record-field $record record-name TPTP:RecordName 0))
(: tptp:record-role (-> Atom %Undefined%))
(= (tptp:record-role $record)
   (tptp-internal:record-field $record record-role TPTP:RecordRole 1))
(: tptp:record-formula (-> Atom %Undefined%))
(= (tptp:record-formula $record)
   (tptp-internal:record-field $record record-formula TPTP:RecordFormula 2))

; The annotations of an input: its source and, when present, its useful info.
(: tptp:record-annotation (-> Atom %Undefined%))
(= (tptp:record-annotation $record)
   (if (tptp-internal:include-record $record)
       (TPTP:NotApplicable record-annotation include)
       (let $shape (tptp-internal:annotated $record)
         (unify $shape (TPTP:Annotated $family $name $role $formula $annotations)
           (unify $annotations ()
             (TPTP:NoAnnotation)
             (unify $annotations ($source)
               (TPTP:RecordAnnotation $source)
               (unify $annotations ($source $info)
                 (TPTP:RecordAnnotation $source $info)
                 $shape)))
           $shape))))
(: tptp:record-source (-> Atom %Undefined%))
(= (tptp:record-source $record)
   (let $annotation (tptp:record-annotation $record)
     (unify $annotation (TPTP:RecordAnnotation $source)
       (TPTP:RecordSource $source)
       (unify $annotation (TPTP:RecordAnnotation $source $info)
         (TPTP:RecordSource $source)
         (unify $annotation (TPTP:NoAnnotation) (TPTP:NoSource) $annotation)))))
(: tptp:record-info (-> Atom %Undefined%))
(= (tptp:record-info $record)
   (let $annotation (tptp:record-annotation $record)
     (unify $annotation (TPTP:RecordAnnotation $source $info)
       (TPTP:RecordInfo $info)
       (unify $annotation (TPTP:RecordAnnotation $source)
         (TPTP:NoInfo)
         (unify $annotation (TPTP:NoAnnotation) (TPTP:NoInfo) $annotation)))))

(: tptp:record-span (-> Atom %Undefined%))
(= (tptp:record-span $_record) (TPTP:SpanUnavailable compact-value))
(: tptp:name-text (-> Atom Atom))
(= (tptp:name-text $name) $name)
(: tptp:role-text (-> Atom Atom))
(= (tptp:role-text $role) $role)

(= (tptp:record-count $records)
   (let $count (size-atom $records) $count))

; Selections keep the records that answer, in order, as a list.  A list of
; records is a value on both hosts, so it needs no Atom parameter and a
; caller may write a call in its place; each walk takes the list apart with
; car-atom and cdr-atom, whose results are values, so a record passes
; through as data.
(= (tptp:select-family $family $records)
   (if (== (size-atom $records) 0)
       $records
       (let $head (car-atom $records)
         (let $tail (cdr-atom $records)
           (let $selected-tail (tptp:select-family $family $tail)
             (if (== $family (tptp:record-family $head))
                 (cons-atom $head $selected-tail)
                 $selected-tail))))))

(: tptp:record-has-name (-> Atom Atom %Undefined%))
(= (tptp:record-has-name $name $record)
   (let $view (tptp:record-name $record)
     (unify $view (TPTP:RecordName $actual)
       (unify $actual $name True False)
       False)))
(= (tptp:select-name $name $records)
   (if (== (size-atom $records) 0)
       $records
       (let $head (car-atom $records)
         (let $tail (cdr-atom $records)
           (let $selected-tail (tptp:select-name $name $tail)
             (if (tptp:record-has-name $name $head)
                 (cons-atom $head $selected-tail)
                 $selected-tail))))))

(: tptp:record-has-role (-> Atom Atom %Undefined%))
(= (tptp:record-has-role $role $record)
   (let $view (tptp:record-role $record)
     (unify $view (TPTP:RecordRole $actual)
       (unify $actual $role True False)
       False)))
(= (tptp:select-role $role $records)
   (if (== (size-atom $records) 0)
       $records
       (let $head (car-atom $records)
         (let $tail (cdr-atom $records)
           (let $selected-tail (tptp:select-role $role $tail)
             (if (tptp:record-has-role $role $head)
                 (cons-atom $head $selected-tail)
                 $selected-tail))))))

(= (tptp:record-statistics $records)
   (tptp:record-statistics-inputs $records 0 0 0 0 0 0 0 0))
(= (tptp:record-statistics-inputs $records
      $total $fof $cnf $tff $thf $tcf $tpi $include)
   (if (== (size-atom $records) 0)
       (TPTP:RecordStatistics
         $total $fof $cnf $tff $thf $tcf $tpi $include)
       (let $head (car-atom $records)
         (let $tail (cdr-atom $records)
           (case (tptp:record-family $head)
             ((fof (tptp:record-statistics-inputs $tail
                     (+ $total 1) (+ $fof 1) $cnf $tff $thf $tcf $tpi $include))
              (cnf (tptp:record-statistics-inputs $tail
                     (+ $total 1) $fof (+ $cnf 1) $tff $thf $tcf $tpi $include))
              (tff (tptp:record-statistics-inputs $tail
                     (+ $total 1) $fof $cnf (+ $tff 1) $thf $tcf $tpi $include))
              (thf (tptp:record-statistics-inputs $tail
                     (+ $total 1) $fof $cnf $tff (+ $thf 1) $tcf $tpi $include))
              (tcf (tptp:record-statistics-inputs $tail
                     (+ $total 1) $fof $cnf $tff $thf (+ $tcf 1) $tpi $include))
              (tpi (tptp:record-statistics-inputs $tail
                     (+ $total 1) $fof $cnf $tff $thf $tcf (+ $tpi 1) $include))
              (include (tptp:record-statistics-inputs $tail
                         (+ $total 1) $fof $cnf $tff $thf $tcf $tpi (+ $include 1)))
              ($unsupported $unsupported)))))))

; Formulae, terms, and types are ordinary MeTTa atoms.  Named logical heads
; receive precise kinds; every other expression is an application/sequence.
; The kind of a compact value by its head: connectives, quantifiers and
; binders, types and typing, the other glyph forms, $let, the tagged leaves,
; rationals and reals; a one-element expression is a long connective and any
; other expression an application.  A list value is a list, whose children
; are its elements.  Atoms are symbols, host literals
; (numbers, strings, which are distinct objects under the mixed policy) and
; host variables.
(: tptp:expression-kind (-> Atom %Undefined%))
(= (tptp:expression-kind $expression)
   (case (get-metatype $expression)
     ((Expression
        (let $size (size-atom $expression)
          (if (== $size 0)
              sequence
              (let $head (car-atom $expression)
                (let $kind (tptp-internal:head-kind $head)
                  (unify $kind (TPTP:Kind $named)
                    $named
                    (if (== $size 1) long-connective application)))))))
      ($metatype (tptp:expression-kind-metatype $metatype)))))
(= (tptp:expression-kind-metatype List) list)
(= (tptp:expression-kind-metatype Symbol) symbol)
(= (tptp:expression-kind-metatype Grounded) literal)
(= (tptp:expression-kind-metatype Variable) host-variable)

; The named heads and their kinds; any other head is applied.  A head is a
; symbol or a tagged leaf, which neither host evaluates.
(: tptp-internal:head-kind (-> Atom %Undefined%))
(= (tptp-internal:head-kind $head)
   (case $head
     ((¬ (TPTP:Kind not)) (∧ (TPTP:Kind and)) (∨ (TPTP:Kind or))
      (→ (TPTP:Kind implies)) (← (TPTP:Kind reverse-implies))
      (↔ (TPTP:Kind iff)) (⊕ (TPTP:Kind xor)) (⊼ (TPTP:Kind nand))
      (⊽ (TPTP:Kind nor)) (≈ (TPTP:Kind equals)) (≉ (TPTP:Kind not-equals))
      (∀ (TPTP:Kind forall)) (∃ (TPTP:Kind exists)) (λ (TPTP:Kind lambda))
      (ε (TPTP:Kind choice)) (ι (TPTP:Kind description))
      (Π (TPTP:Kind type-forall)) (Σ (TPTP:Kind type-exists))
      ((defined "#") (TPTP:Kind hash-quantifier))
      (: (TPTP:Kind typing)) (⇒ (TPTP:Kind map-type))
      (× (TPTP:Kind product-type)) (⊎ (TPTP:Kind union-type))
      (⊑ (TPTP:Kind subtype)) (≡ (TPTP:Kind definition))
      (≔ (TPTP:Kind assignment)) (⊢ (TPTP:Kind sequent))
      (□ (TPTP:Kind box)) (◇ (TPTP:Kind diamond))
      (§let (TPTP:Kind let)) ((defined "let") (TPTP:Kind let))
      (rat (TPTP:Kind rational)) (real (TPTP:Kind real))
      (word (TPTP:Kind word)) (var (TPTP:Kind variable))
      (defined (TPTP:Kind defined)) (system (TPTP:Kind system))
      (distinct (TPTP:Kind distinct-object))
      ($_ (TPTP:NotAKind)))))

; The children of a compact value as a list: an application's arguments
; after its head, a list's elements, and nothing for an atom.
(: tptp:expression-children (-> Atom %Undefined%))
(= (tptp:expression-children $expression)
   (case (get-metatype $expression)
     ((Expression (__cetta_lib_list_from_expression (cdr-atom $expression)))
      (List $expression)
      ($_ (__cetta_lib_list_from_expression ())))))

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

; The prepared frozen reader above is the default problem-file path.  The
; explicit construction and evidence interfaces remain available for checking
; its connection to the authored official-grammar transformations.

; ------------------------------------------------------ include loading --

; Build the compact resolver environment by parsing each canonical source at
; most once.  Include occurrences remain in their source documents and are
; expanded later, so repeated directives still produce repeated formula
; occurrences even though their target file is read only once.

(= (tptp:compact-include-load-member $needle $items)
   (if (== $items ())
       False
       (let ($head $rest) (decons-atom $items)
         (if (== $needle $head)
             True
             (tptp:compact-include-load-member $needle $rest)))))

(= (tptp:compact-include-binding-member $from $requested $bindings)
   (if (== $bindings ())
       False
       (let ($head $rest) (decons-atom $bindings)
         (case $head
           (((tptp-ci-v1:include-binding
                $candidate-from $candidate-requested $_target $_mode)
              (if (== $from $candidate-from)
                  (if (== $requested $candidate-requested)
                      True
                      (tptp:compact-include-binding-member
                        $from $requested $rest))
                  (tptp:compact-include-binding-member
                    $from $requested $rest)))
            ($_
              (tptp:compact-include-binding-member
                $from $requested $rest)))))))

(= (tptp:compact-include-load-reverse $items)
   (tptp:compact-include-load-reverse-acc $items ()))
(= (tptp:compact-include-load-reverse-acc $items $accumulator)
   (if (== $items ())
       $accumulator
       (let ($head $rest) (decons-atom $items)
         (tptp:compact-include-load-reverse-acc
           $rest (cons-atom $head $accumulator)))))

(= (tptp:compact-include-documents-carrier $documents)
   (if (== $documents ())
       (tptp-ci-v1:documents-nil)
       (let ($head $rest) (decons-atom $documents)
         (tptp-ci-v1:documents-cons $head
           (tptp:compact-include-documents-carrier $rest)))))
(= (tptp:compact-include-bindings-carrier $bindings)
   (if (== $bindings ())
       (tptp-ci-v1:bindings-nil)
       (let ($head $rest) (decons-atom $bindings)
         (tptp-ci-v1:bindings-cons $head
           (tptp:compact-include-bindings-carrier $rest)))))

(= (tptp:compact-include-load-state-environment
      (TPTP:CompactIncludeLoadStateV1 $documents $bindings $_seen))
   (tptp-ci-v1:environment
     (tptp:compact-include-documents-carrier
       (tptp:compact-include-load-reverse $documents))
     (tptp:compact-include-bindings-carrier
       (tptp:compact-include-load-reverse $bindings))))

(= (tptp:load-compact-include-environment $root-path)
   (tptp:load-compact-include-environment
     $root-path TPTP:NoLibraryRoot))
(= (tptp:load-compact-include-environment $root-path $library-root)
   (case (tptp:open-reader strict)
     (((NativeHandle $kind $id)
        (tptp:load-compact-include-environment-with-reader
          (NativeHandle $kind $id) $root-path $library-root))
      ($fault
        (TPTP:IncludeLoadFaultV1
          (TPTP:IncludeReaderFaultV1 $fault))))))

(= (tptp:load-compact-include-environment-with-reader
      $reader $root-path $library-root)
   (tptp:load-compact-include-environment-with-reader
     $reader mixed $root-path $library-root))
(= (tptp:load-compact-include-environment-with-reader
      $reader $policy $root-path $library-root)
   (case (fs:try-canonical-path $root-path)
     (((FS:CanonicalPath $root-source)
        (tptp:load-compact-include-environment-canonical
          $reader $policy $root-source $library-root))
      ((FS:CanonicalPathFault $message)
        (TPTP:IncludeLoadFaultV1
          (TPTP:IncludeRootUnavailableV1 $root-path $message)))
      ($other
        (TPTP:IncludeLoadFaultV1
          (TPTP:IncludeCanonicalizationFaultV1 $root-path $other))))))

(= (tptp:load-compact-include-environment-canonical
      $reader $policy $root-source $library-root)
   (let $program (tptp:open-compact-lexical-values)
     (case $program
       (((NativeHandle $_kind $_id)
          (let $answers
               (collapse
                 (tptp:compact-include-load-source
                   $reader $policy $program $root-source $library-root
                   (TPTP:CompactIncludeLoadStateV1 () () ())))
            (let $_ (langdef:close-deterministic-equations $program)
              (tptp:compact-include-load-finish
                $root-source $library-root $answers))))
        ($fault
          (TPTP:IncludeLoadFaultV1
            (TPTP:IncludeLexicalProgramFaultV1 $fault)))))))

(= (tptp:compact-include-load-finish
      $root-source $library-root $answers)
   (case $answers
     ((((TPTP:CompactIncludeLoadStateV1 $documents $bindings $seen))
        (TPTP:CompactIncludeEnvironmentV1
          $root-source $library-root
          (tptp:compact-include-load-state-environment
            (TPTP:CompactIncludeLoadStateV1
              $documents $bindings $seen))))
      (((TPTP:IncludeLoadFaultV1 $fault))
        (TPTP:IncludeLoadFaultV1 $fault))
      ($other
        (TPTP:IncludeLoadFaultV1
          (TPTP:IncludeLoadCardinalityFaultV1
            $root-source $library-root $other))))))

(= (tptp:compact-include-load-source
      $reader $policy $program $source $library-root
      (TPTP:CompactIncludeLoadStateV1 $documents $bindings $seen))
   (if (tptp:compact-include-load-member $source $seen)
       (TPTP:CompactIncludeLoadStateV1 $documents $bindings $seen)
       (tptp:compact-include-load-document
         $reader $policy $program $source $library-root
         (TPTP:CompactIncludeLoadStateV1
           $documents $bindings (cons-atom $source $seen))
         (tptp:read-file-document $reader $policy $source))))

(= (tptp:compact-include-load-document
      $reader $policy $program $source $library-root
      (TPTP:CompactIncludeLoadStateV1 $documents $bindings $seen) $outcome)
   (case $outcome
     (((TPTP:SourceDocument $_identity $digest $text
          $inputs $_spans)
        (let $document
             (tptp-ci-v1:source-document $source $digest
               $inputs $text)
          (tptp:compact-include-load-inputs
            $reader $policy $program $source $inputs $library-root
            (TPTP:CompactIncludeLoadStateV1
              (cons-atom $document $documents) $bindings $seen))))
      ($fault
        (TPTP:IncludeLoadFaultV1
          (TPTP:IncludeSourceReadFaultV1 $source $fault))))))

(= (tptp:compact-include-load-inputs
      $reader $policy $program $source $inputs
      $library-root $state)
   (if (== (size-atom $inputs) 0)
       $state
       (tptp:compact-include-load-input
         $reader $policy $program $source $inputs $library-root $state)))
(= (tptp:compact-include-load-input
      $reader $policy $program $source $inputs
      $library-root $state)
   (let ($head $tail) (decons-atom $inputs)
     (case $head
       (((incl $requested)
          (tptp:compact-include-load-physical
            $reader $policy $program $source $requested $tail $library-root $state
            (tptp:resolve-physical-include
              $source $requested $library-root)))
        ((incl $requested $_selection)
          (tptp:compact-include-load-physical
            $reader $policy $program $source $requested $tail $library-root $state
            (tptp:resolve-physical-include
              $source $requested $library-root)))
        ((incl $requested $_selection $_qualification)
          (tptp:compact-include-load-physical
            $reader $policy $program $source $requested $tail $library-root $state
            (tptp:resolve-physical-include
              $source $requested $library-root)))
        ($_
          (tptp:compact-include-load-inputs
            $reader $policy $program $source $tail $library-root $state))))))

(= (tptp:compact-include-load-physical
      $reader $policy $program $source $requested $tail $library-root
      (TPTP:CompactIncludeLoadStateV1 $documents $bindings $seen) $outcome)
   (case $outcome
     (((TPTP:PhysicalIncludeBoundV1 $mode $target)
        (let $next-bindings
             (if (tptp:compact-include-binding-member
                   $source $requested $bindings)
                 $bindings
                 (cons-atom
                   (tptp-ci-v1:include-binding
                     $source $requested $target $mode)
                   $bindings))
          (tptp:compact-include-load-after-target
            $reader $policy $program $source $tail $library-root
            (tptp:compact-include-load-source
              $reader $policy $program $target $library-root
              (TPTP:CompactIncludeLoadStateV1
                $documents $next-bindings $seen)))))
      ($fault
        (TPTP:IncludeLoadFaultV1
          (TPTP:IncludePhysicalResolutionFaultV1
            $source $requested $fault))))))

(= (tptp:compact-include-load-after-target
      $reader $policy $program $source $tail $library-root
      (TPTP:CompactIncludeLoadStateV1 $documents $bindings $seen))
   (tptp:compact-include-load-inputs
     $reader $policy $program $source $tail $library-root
     (TPTP:CompactIncludeLoadStateV1 $documents $bindings $seen)))
(= (tptp:compact-include-load-after-target
      $_reader $_policy $_program $_source $_tail $_library-root
      (TPTP:IncludeLoadFaultV1 $fault))
   (TPTP:IncludeLoadFaultV1 $fault))

(= (tptp:resolve-loaded-compact-includes $loaded)
   (case $loaded
     (((TPTP:CompactIncludeEnvironmentV1
          $root-source $_library-root $environment)
        (tptp:resolve-compact-include-environment
          $root-source $environment))
      ($fault $fault))))

(= (tptp:read-file-expanded $root-path)
   (tptp:resolve-loaded-compact-includes
     (tptp:load-compact-include-environment $root-path)))
(= (tptp:read-file-expanded $root-path $library-root)
   (tptp:resolve-loaded-compact-includes
     (tptp:load-compact-include-environment
       $root-path $library-root)))
(= (tptp:read-file-expanded-with-reader
      $reader $root-path $library-root)
   (tptp:read-file-expanded-with-reader
     $reader mixed $root-path $library-root))
; Include expansion under a leaf policy: every included file is read with the
; reader and policy of the root.
(= (tptp:read-file-expanded-with-reader
      $reader $policy $root-path $library-root)
   (tptp:resolve-loaded-compact-includes
     (tptp:load-compact-include-environment-with-reader
       $reader $policy $root-path $library-root)))
; An expanded input holds its record as data: the resolver hands each
; record back inside this carrier, whose fields are never evaluated.
(: TPTP:ExpandedInputV1 (-> Atom Atom Atom Atom Atom Atom))
; The records of an expanded document as a list, in expansion order, each
; as data; a resolution fault passes through unchanged.
(: tptp:expanded-records (-> Atom %Undefined%))
(= (tptp:expanded-records $expanded)
   (unify $expanded (TPTP:ExpandedDocumentV1 $root $inputs $edges)
     (tptp-internal:expanded-input-records $inputs)
     $expanded))
(= (tptp-internal:expanded-input-records (tptp-ci-v1:expanded-inputs-nil))
   (__cetta_lib_list_from_expression ()))
(= (tptp-internal:expanded-input-records
      (tptp-ci-v1:expanded-inputs-cons
        (TPTP:ExpandedInputV1 $origin $path $namespace $name $record) $tail))
   (let $rest (tptp-internal:expanded-input-records $tail)
     (cons-atom $record $rest)))
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
     ((official-derivation
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
          ((official-derivation
             (tptp:admit-official-derivation $value))
           ($_
             (TPTP:ArtifactRejected UnsupportedStage $stage)))))
      ($other
        (TPTP:ArtifactRejected NotStageValue $other)))))

; ------------------------------------------------------------- inspection --

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
