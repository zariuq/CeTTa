:- module(parser_pack_guard_plan_reference_v1,
          [ parser_pack_guard_plan_reference_v1/9
          ]).

:- use_module(finite_horn_gslt_v1, [render_term/2]).
:- use_module(parser_pack_eval,
              [ parser_pack_canonical_terms/2,
                parser_pack_digest/2,
                parser_pack_missing_states/3
              ]).
:- use_module(parser_pack_guard_reference_compile,
              [parser_pack_guard_artifact_valid/1]).
:- use_module(library(crypto)).
:- use_module(library(pairs)).
:- use_module(library(utf8)).

parser_pack_guard_plan_reference_v1(
    Pack,
    StartState,
    LexicalTags0,
    RegularCompilerDigest,
    LexicalNFAAnswerDigest,
    GuardProvenance,
    GuardNFAAnswerDigest,
    GuardNFATags,
    Plan) :-
    Pack = parser_pack_v1(Productions, _),
    parser_pack_digest(Pack, PackDigest),
    pack_identity_tables(Pack, States, Terminals),
    length(States, BaseStateLen),
    length(Terminals, BaseTerminalLen),
    length(Productions, BaseProductionLen),
    parser_pack_canonical_terms(LexicalTags0, LexicalTags),
    same_length(LexicalTags0, LexicalTags),
    lexical_entries(
        LexicalTags, States, Productions, 0, RawLexicalEntries),
    map_list_to_pairs(lexical_entry_state_text,
                      RawLexicalEntries, LexicalPairs),
    keysort(LexicalPairs, SortedLexicalPairs),
    pairs_values(SortedLexicalPairs, SortedLexicalEntries),
    number_lexical_entries(
        SortedLexicalEntries, 0, LexicalEntries),
    lexical_plan_digest(
        PackDigest, RegularCompilerDigest, LexicalNFAAnswerDigest,
        LexicalEntries, LexicalPlanDigest),
    length(LexicalEntries, LexicalTerminalLen),
    guard_provenance_fields(
        GuardProvenance,
        SourceDigest, PreReflectionDigest, EnvironmentDigest,
        GuardAnswerSetDigest, GuardAnswers),
    guard_seeds(GuardAnswers, GuardSeeds),
    guard_seed_tags(GuardSeeds, PlannedGuardTags),
    parser_pack_canonical_terms(GuardNFATags, CanonicalGuardNFATags),
    same_length(GuardNFATags, CanonicalGuardNFATags),
    PlannedGuardTags == CanonicalGuardNFATags,
    number_guard_entries(
        GuardSeeds, States, Productions,
        BaseStateLen, BaseTerminalLen, BaseProductionLen,
        LexicalTerminalLen, 0, 0, GuardEntries, StateExtensionLen),
    guard_plan_digest(
        PackDigest, LexicalPlanDigest,
        SourceDigest, PreReflectionDigest, EnvironmentDigest,
        GuardAnswerSetDigest, RegularCompilerDigest,
        GuardNFAAnswerDigest, LexicalTerminalLen,
        GuardEntries, GuardPlanDigest),
    guard_entry_productions(GuardEntries, GuardProductions),
    append(Productions, GuardProductions, ExtendedProductions),
    Pack = parser_pack_v1(_, ClassClauses),
    parser_pack_missing_states(
        parser_pack_v1(ExtendedProductions, ClassClauses),
        [StartState], []),
    length(GuardEntries, GuardEntryLen),
    Plan = parser_pack_guard_plan_reference_v1(
        base_pack_digest(PackDigest),
        base_counts(BaseStateLen, BaseTerminalLen, BaseProductionLen),
        lexical_plan(
            LexicalNFAAnswerDigest, LexicalPlanDigest,
            LexicalEntries),
        guard_provenance(
            SourceDigest, PreReflectionDigest, EnvironmentDigest,
            GuardAnswerSetDigest),
        guard_nfa(GuardNFAAnswerDigest, CanonicalGuardNFATags),
        guard_plan(
            GuardPlanDigest, GuardEntryLen, StateExtensionLen,
            GuardEntries),
        start_closed).

pack_identity_tables(parser_pack_v1(Productions, _), States, Terminals) :-
    findall(State,
            ( member(Production, Productions),
              production_state_identity(Production, State)
            ),
            RawStates),
    parser_pack_canonical_terms(RawStates, States),
    findall(Terminal,
            ( member(Production, Productions),
              production_terminal_identity(Production, Terminal)
            ),
            RawTerminals),
    parser_pack_canonical_terms(RawTerminals, Terminals).

production_state_identity(
    list([sym('pp-production'), _, State, _, _]), State).
production_state_identity(
    list([sym('pp-production'), _, _, Items, _]), State) :-
    items_nonterminal(Items, State).

items_nonterminal(
    list([sym('pp-items-cons'),
          list([sym('pp-nonterminal'), State]), _]), State).
items_nonterminal(
    list([sym('pp-items-cons'), _, More]), State) :-
    items_nonterminal(More, State).

production_terminal_identity(
    list([sym('pp-production'), _, _, Items, _]), Terminal) :-
    items_terminal(Items, Terminal).

items_terminal(
    list([sym('pp-items-cons'),
          list([sym('pp-terminal'), Terminal]), _]), Terminal).
items_terminal(
    list([sym('pp-items-cons'), _, More]), Terminal) :-
    items_terminal(More, Terminal).

state_defined(Productions, State) :-
    member(list([sym('pp-production'), _, State, _, _]), Productions),
    !.

lexical_entries([], _, _, _, []).
lexical_entries([Tag|Tags], States, Productions, TagIndex,
                [lexical_entry_seed(
                     TagIndex, StateId, Tag, State, StateText)|Entries]) :-
    State = list([sym('pp-def'), Tag]),
    nth0(StateId, States, State),
    state_defined(Productions, State),
    render_term(State, StateText),
    NextTagIndex is TagIndex + 1,
    lexical_entries(
        Tags, States, Productions, NextTagIndex, Entries).

lexical_entry_state_text(
    lexical_entry_seed(_, _, _, _, StateText), StateText).

number_lexical_entries([], _, []).
number_lexical_entries(
    [lexical_entry_seed(TagIndex, StateId, Tag, State, _)|Entries0],
    TerminalIndex,
    [lexical_entry(TagIndex, StateId, TerminalIndex, Tag, State)|Entries]) :-
    NextTerminalIndex is TerminalIndex + 1,
    number_lexical_entries(Entries0, NextTerminalIndex, Entries).

guard_provenance_fields(
    parser_pack_guard_compilation(
        SourcePresentations, Program, GuardAnswers, _),
    SourceDigest, PreReflectionDigest, EnvironmentDigest,
    GuardAnswerSetDigest, GuardAnswers) :-
    parser_pack_guard_reference_compile:
        parser_pack_guard_compilation_provenance(
            SourcePresentations, Program, GuardAnswers,
            parser_pack_guard_provenance_v1(
                source_digest(SourceDigest),
                pre_reflection_digest(PreReflectionDigest),
                environment_digest(EnvironmentDigest),
                answer_set_digest(GuardAnswerSetDigest),
                guard_evidence(_))).

guard_seeds(GuardAnswers, GuardSeeds) :-
    findall(StateText-guard_seed(
                Owner, State, Tag, Body, Production),
            ( member(answer(Answer, _), GuardAnswers),
              Answer = list([sym('compile-pack-positive-guard'),
                             Owner, Artifact]),
              parser_pack_guard_artifact_valid(Artifact),
              Artifact = list([sym('pp-positive-guard-v1'),
                               State, Tag, Body, Production]),
              render_term(State, StateText)
            ),
            SeedPairs0),
    sort(SeedPairs0, SeedPairs),
    group_pairs_by_key(SeedPairs, Groups),
    maplist(unique_guard_seed, Groups, GuardSeeds),
    GuardSeeds = [_|_].

unique_guard_seed(_-[Seed], Seed).

guard_seed_tags(GuardSeeds, Tags) :-
    findall(Tag,
            member(guard_seed(_, _, Tag, _, _), GuardSeeds),
            RawTags),
    parser_pack_canonical_terms(RawTags, Tags),
    same_length(RawTags, Tags).

number_guard_entries([], _, _, _, _, _, _, _, ExtensionLen, [],
                     ExtensionLen).
number_guard_entries(
    [guard_seed(Owner, State, Tag, Body, Production)|Seeds],
    States, Productions,
    BaseStateLen, BaseTerminalLen, BaseProductionLen,
    LexicalTerminalLen, EntryIndex, ExtensionIndex,
    [guard_entry(
         StateId, TerminalId, ProductionId, StateIsExtension,
         Owner, State, Tag, Body, Production)|Entries],
    StateExtensionLen) :-
    ( nth0(BaseStateId, States, State) ->
        \+ state_defined(Productions, State),
        StateId = BaseStateId,
        StateIsExtension = 0,
        NextExtensionIndex = ExtensionIndex
    ; StateId is BaseStateLen + ExtensionIndex,
      StateIsExtension = 1,
      NextExtensionIndex is ExtensionIndex + 1
    ),
    TerminalId is BaseTerminalLen + LexicalTerminalLen + EntryIndex,
    ProductionId is BaseProductionLen + EntryIndex,
    NextEntryIndex is EntryIndex + 1,
    number_guard_entries(
        Seeds, States, Productions,
        BaseStateLen, BaseTerminalLen, BaseProductionLen,
        LexicalTerminalLen, NextEntryIndex, NextExtensionIndex,
        Entries, StateExtensionLen).

guard_entry_productions([], []).
guard_entry_productions(
    [guard_entry(_, _, _, _, _, _, _, _, Production)|Entries],
    [Production|Productions]) :-
    guard_entry_productions(Entries, Productions).

lexical_plan_digest(PackDigest, RegularCompilerDigest,
                    NFAAnswerDigest, Entries, Digest) :-
    utf8_bytes("ParserPackLexicalProjectionV1\n", Domain),
    text_line_bytes(PackDigest, PackBytes),
    text_line_bytes(RegularCompilerDigest, CompilerBytes),
    text_line_bytes(NFAAnswerDigest, NFABytes),
    lexical_entry_bytes(Entries, EntryBytes),
    append([Domain, PackBytes, CompilerBytes, NFABytes, EntryBytes], Bytes),
    crypto_data_hash(Bytes, Digest,
                     [algorithm(sha256), encoding(octet)]).

lexical_entry_bytes([], []).
lexical_entry_bytes(
    [lexical_entry(TagIndex, _, _, _, State)|Entries], Bytes) :-
    uint32_be(TagIndex, IndexBytes),
    render_term(State, StateText),
    text_line_bytes(StateText, StateBytes),
    lexical_entry_bytes(Entries, MoreBytes),
    append([IndexBytes, StateBytes, MoreBytes], Bytes).

guard_plan_digest(
    PackDigest, LexicalPlanDigest,
    SourceDigest, PreReflectionDigest, EnvironmentDigest,
    GuardAnswerSetDigest, RegularCompilerDigest,
    GuardNFAAnswerDigest, LexicalTerminalLen,
    Entries, Digest) :-
    framed_text("ParserPackPositiveGuardPlanV1", Domain),
    framed_text(PackDigest, PackBytes),
    uint32_be(1, HasLexicalBytes),
    framed_text(LexicalPlanDigest, LexicalPlanBytes),
    framed_text(SourceDigest, SourceBytes),
    framed_text(PreReflectionDigest, PreReflectionBytes),
    framed_text(EnvironmentDigest, EnvironmentBytes),
    framed_text(GuardAnswerSetDigest, GuardAnswerBytes),
    framed_text(RegularCompilerDigest, RegularCompilerBytes),
    framed_text(GuardNFAAnswerDigest, GuardNFABytes),
    uint32_be(LexicalTerminalLen, LexicalLenBytes),
    length(Entries, EntryLen),
    uint32_be(EntryLen, EntryLenBytes),
    guard_entry_bytes(Entries, EntryBytes),
    append([Domain, PackBytes, HasLexicalBytes, LexicalPlanBytes,
            SourceBytes, PreReflectionBytes, EnvironmentBytes,
            GuardAnswerBytes, RegularCompilerBytes, GuardNFABytes,
            LexicalLenBytes, EntryLenBytes, EntryBytes], Bytes),
    crypto_data_hash(Bytes, Digest,
                     [algorithm(sha256), encoding(octet)]).

guard_entry_bytes([], []).
guard_entry_bytes(
    [guard_entry(StateId, TerminalId, ProductionId, StateIsExtension,
                 Owner, State, Tag, Body, Production)|Entries], Bytes) :-
    uint32_be(StateId, StateIdBytes),
    uint32_be(TerminalId, TerminalIdBytes),
    uint32_be(ProductionId, ProductionIdBytes),
    uint32_be(StateIsExtension, StateExtensionBytes),
    framed_term(Owner, OwnerBytes),
    framed_term(State, StateBytes),
    framed_term(Tag, TagBytes),
    framed_term(Body, BodyBytes),
    framed_term(Production, ProductionBytes),
    guard_entry_bytes(Entries, MoreBytes),
    append([StateIdBytes, TerminalIdBytes, ProductionIdBytes,
            StateExtensionBytes, OwnerBytes, StateBytes, TagBytes,
            BodyBytes, ProductionBytes, MoreBytes], Bytes).

framed_term(Term, Bytes) :-
    render_term(Term, Text),
    framed_text(Text, Bytes).

framed_text(Text0, Bytes) :-
    text_string(Text0, Text),
    utf8_bytes(Text, Payload),
    length(Payload, Length),
    uint64_be(Length, LengthBytes),
    append(LengthBytes, Payload, Bytes).

text_line_bytes(Text0, Bytes) :-
    text_string(Text0, Text),
    string_concat(Text, "\n", Line),
    utf8_bytes(Line, Bytes).

utf8_bytes(Text, Bytes) :-
    string_codes(Text, ScalarCodes),
    phrase(utf8_codes(ScalarCodes), Bytes).

text_string(Value, Text) :-
    ( string(Value) -> Text = Value
    ; atom(Value) -> atom_string(Value, Text)
    ).

uint32_be(Value, Bytes) :-
    Value >= 0,
    Value < 4294967296,
    findall(Byte,
            ( between(0, 3, Index),
              Shift is (3 - Index) * 8,
              Byte is (Value >> Shift) /\ 255
            ),
            Bytes).

uint64_be(Value, Bytes) :-
    Value >= 0,
    Value < 18446744073709551616,
    findall(Byte,
            ( between(0, 7, Index),
              Shift is (7 - Index) * 8,
              Byte is (Value >> Shift) /\ 255
            ),
            Bytes).
