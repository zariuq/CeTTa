#!/usr/bin/env python3
"""Render the encoded ordered-match machine library deterministically.

mm-*/bd-* forms below are the constructors of OrderedMatchMachineLanguage.
Rendering embeds them in its existing PApp/LCons Pattern wire; this is a
library-generation notation, never a parser for submitted module rules.
"""
from pathlib import Path
import argparse
from gslt2parse_schema_v1 import Symbol, StringLiteral, parse_sexprs, render

SOURCE = r'''
(= (ordered:fused:nat $n)
   (if (== $n 0) (zero) (let $p (- $n 1) (let $v (ordered:fused:nat $p) (succ $v)))))
(= (ordered:fused:number $n)
   (case $n (((zero) 0) ((succ $p) (+ 1 (ordered:fused:number $p))))))
(= (ordered:fused:key $subject)
   (case $subject (((PApp $head $children)
     (let $n (ordered:fused:nat (gslt:automata:length $children))
       (mm-key (mm-head-app (PApp $head LNil) $n) $n))))))
(= (ordered:fused:cursor $children $tail)
   (case $children ((LNil $tail) ((LCons $head $rest)
     (let $next (ordered:fused:cursor $rest $tail) (mm-ccons $head $next))))))
(= (ordered:fused:patterns-append $a $b)
   (case $a (((mm-pnil) $b) ((mm-pcons $p $ps)
     (let $tail (ordered:fused:patterns-append $ps $b) (mm-pcons $p $tail))))))
(= (ordered:fused:cursor-values $cursor)
   (case $cursor (((mm-cnil) LNil) ((mm-ccons $v $vs)
     (let $tail (ordered:fused:cursor-values $vs) (LCons $v $tail))))))
(= (ordered:fused:reverse $values $acc)
   (case $values ((LNil $acc) ((LCons $v $vs)
     (ordered:fused:reverse $vs (LCons $v $acc))))))
(= (ordered:fused:project $path $subject)
   (case $path
     (((bd-root) (Some $subject))
      ((bd-child $parent $index)
       (case (ordered:fused:project $parent $subject)
         (((Some (PApp $head $children))
           (gslt:rule:row-at (ordered:fused:number $index) $children))
          ($bad None)))))))
(= (ordered:fused:bindings $encoded)
   (case $encoded
     (((bd-nil) LNil)
      ((bd-bind (PApp $name LNil) $value $rest)
       (let $tail (ordered:fused:bindings $rest) (LCons (Binding $name $value) $tail))))))
(= (ordered:fused:encode-bindings $bindings)
   (case $bindings
     ((LNil (bd-nil))
      ((LCons (Binding $name $value) $rest)
       (let $tail (ordered:fused:encode-bindings $rest)
         (bd-bind (PApp $name LNil) $value $tail))))))
(= (ordered:fused:merge $left $right)
   (case (gslt:automata:merge (ordered:fused:bindings $left) (ordered:fused:bindings $right))
     ((None None) ((Some $merged) (let $e (ordered:fused:encode-bindings $merged) (Some $e))))))
(= (ordered:fused:schema $template)
   (case $template
     (((mm-tvar (PApp $name LNil)) (FVar $name))
      ((mm-tbvar $index) (Var (ordered:fused:number $index)))
      ((mm-tapp (PApp $head LNil) $templates)
       (let $children (ordered:fused:schemas $templates) (PApp $head $children))))))
(= (ordered:fused:schemas $templates)
   (case $templates (((mm-tnil) LNil) ((mm-tcons $t $ts)
     (let* (($head (ordered:fused:schema $t)) ($tail (ordered:fused:schemas $ts)))
       (LCons $head $tail))))))
(= (ordered:fused:argument $schema $value $seed)
   (case $schema
     (((FVar $name) (gslt:rule:relation-argument $name $value $seed))
      ($constructed (gslt:automata:bind $schema $value)))))
(= (ordered:fused:arguments $schemas $values $seed)
   (case ($schemas $values)
     (((LNil LNil) (Some LNil))
      (((LCons $schema $schemas-rest) (LCons $value $values-rest))
       (case (ordered:fused:argument $schema $value $seed)
         ((None None)
          ((Some $head)
           (case (gslt:automata:merge $seed $head)
             ((None None)
              ((Some $extended)
               (case (ordered:fused:arguments $schemas-rest $values-rest $extended)
                 ((None None) ((Some $tail) (gslt:automata:merge $head $tail)))))))))))
      ($bad None))))
(= (ordered:fused:query-rows $relation $schemas $rows $index $seed $queries $template $origin $initial $choices)
   (case $rows
     ((LNil LNil)
      ((LCons (PremiseProviderRow $rowRelation $values) $rest)
       (let $tail (ordered:fused:query-rows $relation $schemas $rest (+ $index 1) $seed $queries $template $origin $initial $choices)
         (if (== $rowRelation $relation)
           (case (ordered:fused:arguments $schemas $values $seed)
             ((None $tail)
              ((Some $extension)
               (case (gslt:automata:merge $seed $extension)
                 ((None $tail)
                  ((Some $merged)
                   (let* (($actual (gslt:rule:instantiate-children $schemas $seed))
                          ($choice (QueryRowChoiceV1 $relation $actual ProviderRow $index $values))
                          ($nextChoices (gslt:automata:append $choices (LCons $choice LNil)))
                          ($encoded (ordered:fused:encode-bindings $merged)))
                     (LCons (FusedFrameV1 (mm-premises $queries $template $encoded) $origin $initial $encoded $nextChoices) $tail))))))))
           $tail))))))
(= (ordered:fused:wrap $states $origin $initial $bindings $choices)
   (case $states ((LNil LNil) ((LCons $state $rest)
     (let $tail (ordered:fused:wrap $rest $origin $initial $bindings $choices)
       (LCons (FusedFrameV1 $state $origin $initial $bindings $choices) $tail))))))

(= (ordered:fused:plain-step $state)
   (case $state
     (((mm-run (mm-failure) $subject $cursor) LNil)
      ((mm-run (mm-drop $next) $subject (mm-ccons $focused $cursor))
       (LCons (mm-run $next $subject $cursor) LNil))
      ((mm-run (mm-drop $next) $subject (mm-cnil)) LNil)
      ((mm-run (mm-try $leaf $patterns $failure) $subject $cursor)
       (LCons (mm-prefilter $patterns $cursor $leaf $subject)
         (LCons (mm-run $failure $subject $cursor) LNil)))
      ((mm-run (mm-switch $branches $default) $subject (mm-cnil))
       (LCons (mm-run $default $subject (mm-cnil)) LNil))
      ((mm-run (mm-switch $branches $default) $subject (mm-ccons $focused $cursor))
       (LCons (mm-dispatch $branches $subject $focused $cursor)
         (LCons (mm-run $default $subject (mm-ccons $focused $cursor)) LNil)))
      ((mm-dispatch (mm-bnil) $subject $focused $cursor) LNil)
      ((mm-dispatch (mm-bcons $key $program $rest) $subject $focused $cursor)
       (if (== (ordered:fused:key $focused) $key)
         (case $focused (((PApp $head $children)
           (let $next (ordered:fused:cursor $children $cursor)
             (LCons (mm-run $program $subject $next) LNil)))))
         (LCons (mm-dispatch $rest $subject $focused $cursor) LNil)))
      ((mm-prefilter (mm-pcons (mm-wild) $patterns) (mm-ccons $focused $cursor) $leaf $subject)
       (LCons (mm-prefilter $patterns $cursor $leaf $subject) LNil))
      ((mm-prefilter (mm-pcons (mm-node $key $children) $patterns) (mm-ccons $focused $cursor) $leaf $subject)
       (if (== (ordered:fused:key $focused) $key)
         (case $focused (((PApp $head $arguments)
           (let* (($ps (ordered:fused:patterns-append $children $patterns))
                  ($cs (ordered:fused:cursor $arguments $cursor)))
             (LCons (mm-prefilter $ps $cs $leaf $subject) LNil))))) LNil))
      ((bd-run (bd-succeed) $subject $kont) (LCons (bd-ret (bd-nil) $subject $kont) LNil))
      ((bd-run (bd-capture $path $name) $subject $kont)
       (case (ordered:fused:project $path $subject)
         (((Some $focused) (LCons (bd-ret (bd-bind $name $focused (bd-nil)) $subject $kont) LNil))
          ($bad LNil))))
      ((bd-run (bd-check-bound $path $expected) $subject $kont)
       (case (ordered:fused:project $path $subject)
         (((Some (Var $actual)) (if (== $actual (ordered:fused:number $expected))
           (LCons (bd-ret (bd-nil) $subject $kont) LNil) LNil)) ($bad LNil))))
      ((bd-run (bd-check-constructor $path (PApp $expected LNil) $arity $children) $subject $kont)
       (case (ordered:fused:project $path $subject)
         (((Some (PApp $actual $arguments))
           (if (== $actual $expected)
             (if (== (gslt:automata:length $arguments) (ordered:fused:number $arity))
               (LCons (bd-run $children $subject $kont) LNil) LNil) LNil)) ($bad LNil))))
      ((bd-run (bd-join $head $tail) $subject $kont)
       (LCons (bd-run $head $subject (bd-kcons (bd-join-right $tail) $kont)) LNil))
      ((bd-ret $bindings $subject (bd-kcons (bd-join-right $tail) $kont))
       (LCons (bd-run $tail $subject (bd-kcons (bd-join-merge $bindings) $kont)) LNil))
      ((bd-ret $tailBindings $subject (bd-kcons (bd-join-merge $headBindings) $kont))
       (case (ordered:fused:merge $headBindings $tailBindings)
         (((Some $merged) (LCons (bd-ret $merged $subject $kont) LNil)) ($bad LNil))))
      ((bd-ret $bindings $subject (bd-knil)) (LCons (bd-done $bindings) LNil))
      ((mm-premises (mm-qnil) $template $bindings)
       (LCons (mm-inst $template $bindings (mm-inil)) LNil))
      ((mm-inst (mm-tvar (PApp $name LNil)) $bindings $kont)
       (case (gslt:automata:binding $name (ordered:fused:bindings $bindings))
         ((None (LCons (mm-iret (FVar $name) $bindings $kont) LNil))
          ((Some $value) (LCons (mm-iret $value $bindings $kont) LNil)))))
      ((mm-inst (mm-tbvar $index) $bindings $kont)
       (let $n (ordered:fused:number $index) (LCons (mm-iret (Var $n) $bindings $kont) LNil)))
      ((mm-inst (mm-tapp $name $arguments) $bindings $kont)
       (LCons (mm-instargs $name (mm-cnil) $arguments $bindings $kont) LNil))
      ((mm-instargs (PApp $name LNil) $accumulated (mm-tnil) $bindings $kont)
       (let $children (ordered:fused:reverse (ordered:fused:cursor-values $accumulated) LNil)
         (LCons (mm-iret (PApp $name $children) $bindings $kont) LNil)))
      ((mm-instargs $name $accumulated (mm-tcons $template $templates) $bindings $kont)
       (LCons (mm-inst $template $bindings (mm-icons (mm-arg $name $accumulated $templates) $kont)) LNil))
      ((mm-iret $value $bindings (mm-icons (mm-arg $name $accumulated $templates) $kont))
       (LCons (mm-instargs $name (mm-ccons $value $accumulated) $templates $bindings $kont) LNil))
      ((mm-iret $value $bindings (mm-inil)) (LCons (mm-done $value) LNil))
      ($stuck LNil))))

(= (ordered:fused:step $provider $frame)
   (case $frame
     (((FusedFrameV1 $state $origin $initial $bindings $choices)
       (case $state
         (((mm-prefilter (mm-pnil) (mm-cnil) (mm-plan $ordinal $decision $queries $template) $subject)
           (let $index (ordered:fused:number $ordinal)
             (LCons (FusedFrameV1 (bd-run $decision $subject (bd-kcons (mm-after $queries $template) (bd-knil)))
               $index (bd-nil) (bd-nil) LNil) LNil)))
          ((bd-ret $captured $subject (bd-kcons (mm-after $queries $template) $kont))
           (LCons (FusedFrameV1 (mm-premises $queries $template $captured)
             $origin $captured $captured $choices) LNil))
          ((mm-premises (mm-qcons (mm-query (PApp $relation LNil) $arguments) $queries) $template $seed)
           (case $provider
             (((PremiseProvider $signatures $rows)
               (let* (($schemas (ordered:fused:schemas $arguments))
                      ($plain (ordered:fused:bindings $seed)))
                 (ordered:fused:query-rows $relation $schemas $rows 0 $plain $queries $template $origin $initial $choices))))))
          ($ordinary
           (ordered:fused:wrap (ordered:fused:plain-step $state) $origin $initial $bindings $choices))))))))
(= (ordered:fused:start $program $subject)
   (FusedObservationV1
     (LCons (FusedFrameV1 (mm-run $program $subject (mm-ccons $subject (mm-cnil)))
       None (bd-nil) (bd-nil) LNil) LNil) LNil 0))
(= (ordered:fused:tick $provider $observation)
   (case $observation
     (((FusedObservationV1 LNil $receipts $work) $observation)
      ((FusedObservationV1 (LCons (FusedFrameV1 (mm-done $target) $origin $initial $bindings $choices) $rest) $receipts $work)
       (let* (($i (ordered:fused:bindings $initial)) ($b (ordered:fused:bindings $bindings))
              ($receipt (FusedRuleEmissionV1 $origin $i $b $choices $target))
              ($next (gslt:automata:append $receipts (LCons $receipt LNil))))
         (FusedObservationV1 $rest $next (+ $work 1))))
      ((FusedObservationV1 (LCons $frame $rest) $receipts $work)
       (let $agenda (gslt:automata:append (ordered:fused:step $provider $frame) $rest)
         (FusedObservationV1 $agenda $receipts (+ $work 1)))))))
(= (ordered:fused:run $provider $work $observation)
   (if (== $work 0) $observation
     (case $observation
       (((FusedObservationV1 LNil $receipts $count) $observation)
        ($pending (let $next (ordered:fused:tick $provider $observation)
          (ordered:fused:run $provider (- $work 1) $next)))))))
(= (ordered:fused:publication $observation)
   (case $observation
     (((FusedObservationV1 LNil $receipts $work) (ProgramPublication $receipts ProgramPublicationComplete))
      ((FusedObservationV1 $agenda $receipts $work) (ProgramPublication $receipts ProgramPublicationPending)))))
'''


def embedded(value):
    if not isinstance(value, tuple):
        return value
    if value and isinstance(value[0], Symbol) and (
            value[0].text.startswith(("mm-", "bd-")) or value[0].text in ("zero", "succ")):
        arguments = Symbol("LNil")
        for argument in reversed(value[1:]):
            arguments = (Symbol("LCons"), embedded(argument), arguments)
        return (Symbol("PApp"), StringLiteral(value[0].text), arguments)
    return tuple(embedded(v) for v in value)


def generate():
    equations = parse_sexprs(SOURCE, source="ordered-match library")
    functions = {eq[1][0].text for eq in equations} | {
        "gslt:automata:length", "gslt:automata:append", "gslt:automata:merge",
        "gslt:automata:bind", "gslt:automata:binding", "gslt:rule:row-at",
        "gslt:rule:relation-argument", "gslt:rule:instantiate-children", "+", "-", "==", "<"}
    counter = 0
    def call(value):
        return isinstance(value, tuple) and value and isinstance(value[0], Symbol) and value[0].text in functions
    def evaluated(value):
        # Atom parameters preserve syntax. Introduce explicit evaluation only
        # for primitive/function calls, leaving every constructor payload inert.
        nonlocal counter
        if not isinstance(value, tuple) or not value:
            return value
        name = value[0].text if isinstance(value[0], Symbol) else None
        if name == "case":
            clauses = tuple((pattern, evaluated(body)) for pattern, body in value[2])
            return (value[0], evaluated(value[1]), clauses)
        if name == "if":
            return (value[0], *(evaluated(v) for v in value[1:]))
        if name == "let":
            return (value[0], value[1], evaluated(value[2]), evaluated(value[3]))
        if name == "let*":
            return (value[0], tuple((p, evaluated(b)) for p, b in value[1]), evaluated(value[2]))
        prefix, arguments = [], []
        for original in value[1:]:
            argument = evaluated(original)
            if call(original):
                counter += 1
                variable = Symbol(f"$ordered_arg_{counter}")
                prefix.append((variable, argument))
                argument = variable
            arguments.append(argument)
        expression = (value[0], *arguments)
        for variable, argument in reversed(prefix):
            expression = (Symbol("let"), variable, argument, expression)
        return expression
    equations = [(eq[0], eq[1], evaluated(eq[2])) for eq in equations]
    declarations = []
    for equation in equations:
        function = equation[1]
        declarations.append((Symbol(":"), function[0],
                             (Symbol("->"), *([Symbol("Atom")] * (len(function) - 1)), Symbol("%Undefined%"))))
    return ("; OrderedMatchMachineLanguage encoded transitions with retained rule/query receipts.\n"
            "; Generated by tools/generate_ordered_module_fused_v1.py.\n"
            "; Loads after the byte-pinned scoped matching primitives.\n\n" +
            "\n".join(render(v) for v in declarations) + "\n\n" +
            "\n\n".join(render(embedded(v)) for v in equations) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    output = Path(__file__).resolve().parents[1] / "lib/ordered_module_fused_v1.metta"
    expected = generate()
    if args.check:
        if output.read_text() != expected:
            raise SystemExit("generated fused library differs")
    else:
        output.write_text(expected)


if __name__ == "__main__":
    main()
