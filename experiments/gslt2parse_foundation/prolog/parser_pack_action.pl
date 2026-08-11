:- module(parser_pack_action,
          [ parser_pack_action_valid/2,
            parser_pack_apply_action/3
          ]).

/* Shared, finite ParserPack semantic-action algebra. */

parser_pack_action_valid(list([sym('pa-slot'), Index]), Arity) :-
    peano_number(Index, Slot),
    Slot < Arity.
parser_pack_action_valid(list([sym('pa-const'), _]), _).
parser_pack_action_valid(
    list([sym('pa-apply'), sym(_), Arguments]), Arity) :-
    action_list_valid(Arguments, Arity).

action_list_valid(sym('pa-nil'), _).
action_list_valid(list([sym('pa-cons'), Action, More]), Arity) :-
    parser_pack_action_valid(Action, Arity),
    action_list_valid(More, Arity).

parser_pack_apply_action(list([sym('pa-slot'), Index]), Values, Value) :-
    peano_number(Index, Slot),
    nth0(Slot, Values, Value).
parser_pack_apply_action(list([sym('pa-const'), Value]), _, Value).
parser_pack_apply_action(
    list([sym('pa-apply'), sym(Head), Arguments]), Values,
    list([sym(Head)|EvaluatedArguments])) :-
    apply_action_list(Arguments, Values, EvaluatedArguments).

apply_action_list(sym('pa-nil'), _, []).
apply_action_list(
    list([sym('pa-cons'), Action, More]), Values,
    [Value|MoreValues]) :-
    parser_pack_apply_action(Action, Values, Value),
    apply_action_list(More, Values, MoreValues).

peano_number(sym('q-zero'), 0).
peano_number(list([sym('q-succ'), Previous]), Number) :-
    peano_number(Previous, Prior),
    Number is Prior + 1.
