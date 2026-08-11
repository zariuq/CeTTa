:- module(test_parser_pack_guard_plan_prime_v1,
          [ parser_pack_guard_plan_profile_row/3,
            parser_pack_guard_plan_prime_row/2
          ]).

:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(parser_pack_eval, [parser_pack_digest/2]).
:- use_module(parser_pack_reference_compile).
:- use_module(parser_pack_guard_reference_compile,
              [ parser_pack_guard_reference_compile/4,
                parser_pack_guard_answer_set_digest/2
              ]).
:- use_module(parser_pack_guard_regular_reference_compile).
:- use_module(parser_pack_guard_plan_reference_v1).
:- use_module(regular_span_oracle_v1, [regular_span_answer_set_v1/6]).
:- use_module(library(crypto)).
:- use_module(library(http/json)).

:- initialization(main, main).

main :-
    catch(main_, Error, (print_message(error, Error), halt(1))),
    halt.

main_ :-
    current_prolog_flag(argv, Arguments),
    ( Arguments = [PresentationRoot, ProfileText],
      atom_string(Profile, ProfileText) -> true
    ; throw(error(gate_failed(expected_presentation_root_and_profile),
                  test_parser_pack_guard_plan_prime_v1))
    ),
    parser_pack_guard_plan_profile_row(PresentationRoot, Profile, Row),
    json_write_dict(current_output, Row, [width(0)]),
    nl.

parser_pack_guard_plan_prime_row(PresentationRoot, Row) :-
    parser_pack_guard_plan_profile_row(PresentationRoot, prime, Row).

parser_pack_guard_plan_profile_row(PresentationRoot, Profile, Row) :-
    profile_spec(
        Profile, ProfileName, SourcePaths, StartName, LexicalTagNames),
    parser_pack_reference_compile(
        PresentationRoot, SourcePaths, 4096,
        completed(parser_pack_compilation(
            PackSource, PackProgram, PackAnswers, Pack))),
    parser_pack_compilation_provenance(
        PackSource, PackProgram, PackAnswers,
        parser_pack_provenance(
            source_digest(PackSourceDigest),
            compiler_digest(_),
            environment_digest(_),
            production_evidence(_),
            class_evidence(_))),
    parser_pack_digest(Pack, PackDigest),
    lexical_answer_terms(
        PresentationRoot, SourcePaths, PackSourceDigest,
        LexicalTagNames, LexicalTerms0),
    sort(LexicalTerms0, LexicalTerms),
    length(LexicalTerms0, LexicalTermLen),
    length(LexicalTerms, LexicalTermLen),
    answer_text_set_digest(LexicalTerms, LexicalNFAAnswerDigest),
    directory_file_path(
        PresentationRoot, 'compiler/regular_span_compiler_v1.metta',
        RegularCompilerPath),
    crypto_file_hash(
        RegularCompilerPath, RegularCompilerDigest,
        [algorithm(sha256)]),
    parser_pack_guard_reference_compile(
        PresentationRoot, SourcePaths, 512,
        completed(GuardCompilation)),
    GuardCompilation = parser_pack_guard_compilation(
        _, _, GuardAnswers, _),
    parser_pack_guard_regular_reference_compile(
        PresentationRoot, SourcePaths, 512,
        completed(parser_pack_guard_regular_compilation(
            _, _, RegularGuardAnswers, GuardNFAAnswers, GuardNFATags))),
    guard_answer_texts(GuardAnswers, GuardAnswerTexts),
    guard_answer_texts(RegularGuardAnswers, GuardAnswerTexts),
    parser_pack_guard_answer_set_digest(
        GuardNFAAnswers, GuardNFAAnswerDigest),
    StartState = list([sym('pp-def'), sym(StartName)]),
    maplist(wrap_sym, LexicalTagNames, LexicalTags),
    parser_pack_guard_plan_reference_v1(
        Pack, StartState, LexicalTags,
        RegularCompilerDigest, LexicalNFAAnswerDigest,
        GuardCompilation, GuardNFAAnswerDigest, GuardNFATags, Plan),
    plan_row(
        Plan, PackDigest, LexicalTerms,
        GuardAnswerTexts, GuardNFAAnswers,
        RegularCompilerDigest, ProfileName, Row).

lexical_answer_terms(_, _, _, [], []).
lexical_answer_terms(
    PresentationRoot, SourcePaths, PackSourceDigest,
    [Tag|Tags], Terms) :-
    regular_span_answer_set_v1(
        PresentationRoot, SourcePaths, Tag, 4096,
        SourceDigest, answer_set(_, TagTerms)),
    require(SourceDigest == PackSourceDigest,
            lexical_source_changed(Tag)),
    lexical_answer_terms(
        PresentationRoot, SourcePaths, PackSourceDigest, Tags, MoreTerms),
    append(TagTerms, MoreTerms, Terms).

wrap_sym(Name, sym(Name)).

plan_row(
    parser_pack_guard_plan_reference_v1(
        base_pack_digest(PackDigest),
        base_counts(BaseStateLen, BaseTerminalLen, BaseProductionLen),
        lexical_plan(
            LexicalNFAAnswerDigest, LexicalPlanDigest,
            LexicalEntries),
        guard_provenance(
            SourceDigest, PreReflectionDigest, EnvironmentDigest,
            GuardAnswerSetDigest),
        guard_nfa(GuardNFAAnswerDigest, GuardNFATags),
        guard_plan(
            GuardPlanDigest, GuardEntryLen, StateExtensionLen,
            GuardEntries),
        start_closed),
    PackDigest, LexicalTerms, GuardAnswerTexts, GuardNFAAnswers,
    RegularCompilerDigest, ProfileName,
    _{protocol:"parser-pack-positive-guard-plan-v1",
      profile:ProfileName,
      base_pack_digest:PackDigest,
      base_state_count:BaseStateLen,
      base_terminal_count:BaseTerminalLen,
      base_production_count:BaseProductionLen,
      regular_compiler_digest:RegularCompilerDigest,
      lexical_nfa_answer_digest:LexicalNFAAnswerDigest,
      lexical_nfa_answer_count:LexicalNFAAnswerLen,
      lexical_nfa_terms:LexicalTerms,
      lexical_plan_digest:LexicalPlanDigest,
      lexical_entries:LexicalEntryRows,
      guard_source_digest:SourceDigest,
      guard_pre_reflection_digest:PreReflectionDigest,
      guard_environment_digest:EnvironmentDigest,
      guard_answer_set_digest:GuardAnswerSetDigest,
      guard_answer_terms:GuardAnswerTexts,
      guard_nfa_answer_digest:GuardNFAAnswerDigest,
      guard_nfa_answer_count:GuardNFAAnswerLen,
      guard_nfa_terms:GuardNFATerms,
      guard_tags:GuardTagTexts,
      guard_plan_digest:GuardPlanDigest,
      guard_entry_count:GuardEntryLen,
      guard_state_extension_count:StateExtensionLen,
      guard_entries:GuardEntryRows,
      start_closed:1}) :-
    length(LexicalTerms, LexicalNFAAnswerLen),
    length(GuardNFAAnswers, GuardNFAAnswerLen),
    guard_answer_texts(GuardNFAAnswers, GuardNFATerms),
    maplist(lexical_entry_row, LexicalEntries, LexicalEntryRows),
    maplist(render_term, GuardNFATags, GuardTagTexts),
    maplist(guard_entry_row, GuardEntries, GuardEntryRows).

guard_answer_texts(Answers, Texts) :-
    findall(Text,
            ( member(answer(Answer, _), Answers),
              render_term(Answer, Text)
            ),
            RawTexts),
    sort(RawTexts, Texts),
    same_length(RawTexts, Texts).

lexical_entry_row(
    lexical_entry(TagIndex, StateId, TerminalIndex, Tag, State),
    [TagIndex, StateId, TerminalIndex, TagText, StateText]) :-
    render_term(Tag, TagText),
    render_term(State, StateText).

guard_entry_row(
    guard_entry(
        StateId, TerminalId, ProductionId, StateIsExtension,
        Owner, State, Tag, Body, Production),
    [StateId, TerminalId, ProductionId, StateIsExtension,
     OwnerText, StateText, TagText, BodyText, ProductionText]) :-
    render_term(Owner, OwnerText),
    render_term(State, StateText),
    render_term(Tag, TagText),
    render_term(Body, BodyText),
    render_term(Production, ProductionText).

answer_text_set_digest(Texts, Digest) :-
    maplist(answer_line, Texts, Lines),
    atomics_to_string(["FiniteHornAnswerSetV1\n"|Lines], Payload),
    crypto_data_hash(Payload, Digest, [algorithm(sha256)]).

answer_line(Text, Line) :-
    atomics_to_string([Text, "\n"], Line).

profile_spec(
    he,
    "he-empty-tokenizer-reader-v1",
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/he_reader_scalar_classes_v1.metta',
      'languages/he_reader_v1.metta'
    ],
    'he-document',
    [ 'he-string',
      'he-comment-boundary',
      'he-word-boundary',
      'he-variable-boundary'
    ]).
profile_spec(
    prime,
    "cetta-prime-reader-v1",
    [ 'core/syntax_core_v1.metta',
      'shared/lookahead_core_v1.metta',
      'shared/ground_relations_v1.metta',
      'shared/cetta_prime_scalar_classes_v1.metta',
      'languages/cetta_prime_reader_v1.metta'
    ],
    'cetta-prime-file',
    [ 'cetta-prime-string-escape',
      'cetta-prime-token-boundary'
    ]).

require(Condition, Label) :-
    ( call(Condition) -> true
    ; throw(error(gate_failed(Label),
                  test_parser_pack_guard_plan_prime_v1))
    ).
