; Prepared-once official TPTP reader: compose is a cache fill, not a test path.
!(import! &self ../../lib/lib_tptp.metta)
!(import! &self ../../lib/lib_bnf.metta)

(= (tptp-prep:document
     (TPTP:StageValue extended-bnf-ast (TPTP:ExtendedBnfAst $_binding $source $document)))
   ($source $document))
(= (tptp-prep:source $role $path)
   (TptpReaderArtifactSourceV1
     $role $path (tptp:syntaxbnf-digest $path)))
(= (tptp-prep:common-sources)
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-prepared-reader
       "langdef/tptp/official_prepared_reader_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source lib-tptp "lib/lib_tptp.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source lib-bnf "lib/lib_bnf.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-extended-bnf-source
       "langdef/tptp/official_extended_bnf_source_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-extended-bnf-parser-profile
       "langdef/tptp/official_extended_bnf_parser_profile_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-extended-bnf-ast
       "langdef/tptp/official_extended_bnf_ast_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-extended-bnf-ast-projection
       "langdef/tptp/official_extended_bnf_ast_projection_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-lexical-to-ebnf
       "langdef/tptp/official_lexical_to_ebnf_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-syntax-to-ebnf
       "langdef/tptp/official_syntax_to_ebnf_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source bnf-ebnf-declaration-view
       "langdef/bnf/ebnf_declaration_view_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source bnf-ebnf-reachable-view
       "langdef/bnf/ebnf_reachable_view_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source bnf-ebnf-lowering
       "langdef/bnf/ebnf_lowering_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source bnf-plain-denotation
       "langdef/bnf/plain_bnf_denotation_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-snapshot-specializer
       "native/tptp_official_snapshot_v1.c")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-snapshot-specializer-header
       "native/tptp_official_snapshot_v1.h")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source parser-pack-table-snapshot
       "experiments/gslt2parse_foundation/native/parser_pack_table_snapshot_v1.c")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source parser-pack-table-snapshot-header
       "experiments/gslt2parse_foundation/native/parser_pack_table_snapshot_v1.h")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source native-grammar-parser
       "src/lib_parse_native_grammar.c")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source native-grammar-parser-header
       "src/lib_parse_native_grammar.h")
     TptpReaderArtifactSourcesNilV1))))))))))))))))))))
(= (tptp-prep:profile-sources strict)
   (tptp-prep:common-sources))
(= (tptp-prep:profile-sources corpus-compatible)
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-corpus-compatibility-native-types
       "langdef/tptp/official_corpus_compatibility_native_types_v1.metta")
   (TptpReaderArtifactSourcesConsV1
     (tptp-prep:source tptp-corpus-compatibility
       "langdef/tptp/official_corpus_compatibility_v1.metta")
     (tptp-prep:common-sources))))
(= (tptp-prep:manifest $profile $source)
   (TptpReaderArtifactManifestV2
     $profile $source (tptp-prep:profile-sources $profile)))
(= (tptp-prep:save $path $manifest (bnf-v1:grammar-input $document $authority))
   (case (bnf:lower-ebnf-document $document $authority)
     (((EBNF:LoweredGrammar $original $policy $plain $origins)
        (tptp:write-atom $path
          (TptpPreparedPackV2 $manifest
            (EBNF:LoweredGrammar $original $policy $plain $origins))))
      ($fault $fault))))
(= (tptp-prep:parser-from-pack
     (TptpPreparedPackV2
       (TptpReaderArtifactManifestV2 $profile $source $_sources)
       (EBNF:LoweredGrammar $original $policy $plain $origins)))
   (case (== $source (tptp:official-syntaxbnf-digest))
     ((True
       (case (bnf:parser-from-document $plain $policy)
         (((NativeHandle $kind $id)
            (EBNF:Parser (NativeHandle $kind $id)
              (EBNF:LoweredGrammar $original $policy $plain $origins)))
          ($failure $failure))))
      (False (TPTP:DigestMismatch $source)))))
(= (tptp:load-prepared-parser $path)
   (tptp-prep:parser-from-pack (tptp:read-atom $path)))
; Public tptp:official-reader lives in lib_tptp.metta (frozen TPP1).
; Compose/GLL remain construction and oracle tools via tptp:load-prepared-parser.
(= (tptp-prep:reach (bnf-v1:grammar-input $document $authority))
   (case (bnf:validate-ebnf-declarations $document $authority)
     (((BNFDeclarationValidationAcceptedV1 (BNFStartSomeV1 $start $_span) $_definitions $_lexicals)
        (let $selected
             (bnf-internal:ebnf-transform
               (LangDef:EquationSources
                 "langdef/bnf/ebnf_declaration_view_v1.metta"
                 "langdef/bnf/ebnf_reachable_view_v1.metta")
               (LangDef:DeterministicCall ebnf-reach-v1:select $document $start))
          (bnf-v1:grammar-input $selected $authority)))
      ($fault $fault))))
(= (tptp-prep:profile strict $_program $input) $input)
(= (tptp-prep:profile corpus-compatible $program $input)
   (langdef:run-deterministic-equations $program
     (LangDef:DeterministicCall
       tptp-corpus-v1:extend-formula-terms $input)))
(= (tptp-prep:profile-output strict $_source $input) $input)
(= (tptp-prep:profile-output corpus-compatible $source $input)
   (TPTP:CorpusCompatibleGrammarV1 $source $input))
(= (tptp-prep:compose-profile $profile $source $document)
   (let $program
        (langdef:load-deterministic-equations
          (LangDef:EquationSources
            "langdef/bnf/plain_bnf_denotation_v1.metta"
            "langdef/bnf/ebnf_lowering_v1.metta"
            "langdef/tptp/official_lexical_to_ebnf_v1.metta"
            "langdef/tptp/official_syntax_to_ebnf_v1.metta"
            "langdef/tptp/official_corpus_compatibility_native_types_v1.metta"
            "langdef/tptp/official_corpus_compatibility_v1.metta"))
     (let $lexical
          (langdef:run-deterministic-equations $program
            (LangDef:DeterministicCall tptp-lex-v1:lower $document $source
              (bnf-v1:lexical-except (bnf-v1:scalars-cons 10 (bnf-v1:scalars-nil)))))
       (let $input
            (langdef:run-deterministic-equations $program
              (LangDef:DeterministicCall tptp-syn-v1:compose $lexical))
         (let $profiled (tptp-prep:profile $profile $program $input)
           (let $_ (langdef:close-deterministic-equations $program)
             (case $profiled
               (((bnf-v1:grammar-input $profiled-document $profiled-authority)
                  (case
                    (tptp-prep:reach
                      (bnf-v1:grammar-input
                        $profiled-document $profiled-authority))
                    (((bnf-v1:grammar-input $selected $selected-authority)
                       (tptp-prep:profile-output $profile $source
                         (bnf-v1:grammar-input
                           $selected $selected-authority)))
                     ($reach-fault $reach-fault))))
                ($fault $fault)))))))))
(= (tptp-prep:compose $source $document)
   (tptp-prep:compose-profile strict $source $document))
(= (tptp-prep:compose-corpus-compatible $source $document)
   (tptp-prep:compose-profile corpus-compatible $source $document))
