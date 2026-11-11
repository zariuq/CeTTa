:- module(chr_source_reference, [main/0]).
:- use_module(library(chr)).
:- use_module(library(http/json)).

% Reference-only staging: load CHR clauses before posting the guest block's
% explicit ground queries. The production MeTTa path does not use this tool.
read_terms(Input, Clauses, Queries) :-
    read_terms(Input, string, Clauses, Queries).

% A double_quotes directive governs how later source text reads, as it does
% when SWI-Prolog loads the file; the mode starts at SWI's default.
read_terms(Input, Quotes, Clauses, Queries) :-
    read_term(Input, Term, [module(chr_source_reference), syntax_errors(error), double_quotes(Quotes)]),
    ( Term == end_of_file -> Clauses = [], Queries = []
    ; Term = '?-'(Goal) ->
        Queries = [Goal|Rest], read_terms(Input, Quotes, Clauses, Rest)
    ; ( Term = (:- set_prolog_flag(double_quotes, Next)) -> true ; Next = Quotes ),
      Clauses = [Term|Rest], read_terms(Input, Next, Rest, Queries)
    ).

write_clause(Output, Clause) :-
    write_term(Output, Clause, [quoted(true), ignore_ops(true), fullstop(true), nl(true)]).

write_program(Path, Clauses) :-
    setup_call_cleanup(open(Path, write, Output, [encoding(utf8)]),
        ( write_clause(Output, (:- module(chr_source_fixture, []))),
          write_clause(Output, (:- use_module(library(chr)))),
          maplist(write_clause(Output), Clauses)
        ), close(Output)).

post_queries([]).
post_queries([Goal|Rest]) :- call(chr_source_fixture:Goal), post_queries(Rest).

observe(Queries, Result) :-
    ( post_queries(Queries) ->
        findall(Constraint, chr_source_fixture:find_chr_constraint(Constraint), Raw),
        msort(Raw, Sorted),
        with_output_to(string(Store), write_canonical(Sorted)),
        Result = _{status:completed, store:Store}
    ; Result = _{status:failed}
    ).

main :-
    current_prolog_flag(argv, [Source, Program]),
    set_prolog_flag(optimise, false),
    setup_call_cleanup(open(Source, read, Input, [encoding(utf8)]),
        read_terms(Input, Clauses, Queries), close(Input)),
    write_program(Program, Clauses),
    load_files(Program, [silent(true)]),
    catch(observe(Queries, Result), Error,
        (term_string(Error, Message), Result = _{status:fault, error:Message})),
    json_write_dict(current_output, Result), nl.

:- initialization(main, main).
