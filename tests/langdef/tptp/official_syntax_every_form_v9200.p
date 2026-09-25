% Every constructor and literal of the canonical terms of the pinned grammar.
tpi(tpi_input,plain,p).
%----THF
thf(thf_or,axiom,p | q | r).
thf(thf_iff,axiom,p <=> q).
thf(thf_and,axiom,p & q & r).
thf(thf_nested_and,axiom,(p & q) & r).
thf(thf_apply_chain,axiom,f @ a @ b).
thf(thf_apply_parenthesized,axiom,(f @ a) @ b).
thf(thf_apply_argument,axiom,f @ (g @ a)).
thf(thf_not,axiom,~ p).
thf(thf_unequal,axiom,a != b).
thf(thf_connective_term,axiom,(&) @ p @ q).
thf(thf_or_term,axiom,(|) @ p @ q).
thf(thf_equal,axiom,(f @ a) = b).
thf(thf_let,axiom,$let([c: $i,d: $i],[c := a,d := b],p @ c @ d)).
thf(thf_let_single,axiom,$let(c: $i,c := a,p @ c)).
thf(thf_tuple,axiom,[a,b,c] = [a,b,c]).
thf(thf_empty_tuple,axiom,p @ []).
thf(thf_functions,axiom,(p(a,b) & ($sum(1,2) = 3)) & $$sys(a)).
thf(thf_typing_parenthesized,type,(c: $i)).
thf(thf_mapping,type,g: $i > $i > $o).
thf(thf_product,type,h: ($i * $i * $i) > $o).
thf(thf_union,type,k: ($i + $i + $i) > $o).
thf(thf_subtype,type,dog << animal).
thf(thf_definition,definition,d == a).
thf(thf_sequent,axiom,[p] --> [q]).
thf(thf_lambda,axiom,(^ [X: $i] : (p @ X)) @ a).
thf(thf_choice,axiom,(@+ [X: $i] : (p @ X)) = a).
thf(thf_description,axiom,(@- [X: $i] : (p @ X)) = a).
thf(thf_type_forall,type,id: !> [A: $tType] : (A > A)).
thf(thf_type_exists,type,e: ?* [A: $tType] : A).
thf(thf_th1_terms,axiom,(!! @ $i @ (^ [X: $i] : (p @ X))) & (?? @ $i @ (^ [X: $i] : (p @ X))) & ((@@+ @ $i @ (^ [X: $i] : (p @ X))) = a) & ((@@- @ $i @ (^ [X: $i] : (p @ X))) = a) & (@= @ $i @ a @ a)).
thf(thf_short_connectives,axiom,([.] p) & (<.> p) & ({.} p) & ((.) p)).
thf(thf_long_connective,axiom,{$box(#1,$agent == b)} @ p).
thf(thf_long_connective_bare,axiom,{$necessary} @ p).
%----TFF
tff(tff_implies,axiom,p => q).
tff(tff_or,axiom,p | q | r).
tff(tff_and,axiom,p & q & r).
tff(tff_parenthesized,axiom,(p & q) & r).
tff(tff_not,axiom,~ p).
tff(tff_unequal,axiom,a != b).
tff(tff_atoms,axiom,p(a,b) & $less(1,2) & $$sys(a)).
tff(tff_equal,axiom,f(a) = b).
tff(tff_let,axiom,$let([c: $i,d: $i],[c := a,d := b],p(c,d))).
tff(tff_let_single,axiom,$let(c: $i,c := a,p(c))).
tff(tff_parenthesized_term,axiom,(p) = q).
tff(tff_tuple,axiom,[a,b] = [a,b]).
tff(tff_typing_parenthesized,type,(c: $i)).
tff(tff_non_atomic,type,f: (($i * $i) > $i)).
tff(tff_polymorphic,type,id: !> [A: $tType] : (A > A)).
tff(tff_unitary_type,type,g: ($i * $i) > $o).
tff(tff_type_application,type,l: list($i) > $o).
tff(tff_parenthesized_type,type,m: ($i) > $o).
tff(tff_product,type,h: ($i * $i * $i) > $o).
tff(tff_tuple_type,type,t: [$i,$o]).
tff(tff_subtype,type,dog << animal).
tff(tff_definition,definition,d == a).
tff(tff_sequent,axiom,[p] --> [q]).
tff(tff_long_connective,axiom,{$box(#1)} @ (p)).
tff(tff_long_connective_bare,axiom,{$possible} @ (q)).
tff(tff_hash,axiom,# [X: $i] : p(X)).
tff(tff_short_connective,axiom,[.] p).
%----TCF
tcf(tcf_quantified,axiom,! [X: $i] : (p(X) | q)).
tcf(tcf_nested,axiom,! [X: $i] : ! [Y: $i] : p(X,Y)).
%----FOF
fof(fof_and,axiom,p & q & r).
fof(fof_or,axiom,p | q | r).
fof(fof_not,axiom,~ p).
fof(fof_unequal,axiom,a != b).
fof(fof_quantified,axiom,! [X] : ? [Y] : p(X,Y)).
fof(fof_equal,axiom,f(a) = b).
fof(fof_defined,axiom,$less(a,b)).
fof(fof_system,axiom,$$sys(a)).
fof(fof_sequent,axiom,[p,q] --> [r]).
fof(fof_sequent_parenthesized,axiom,([p] --> [])).
fof(fof_connectives,axiom,(p <= q) & (p <~> q) & (p ~| q) & (p ~& q) & (p <=> q)).
%----CNF
cnf(cnf_unit,axiom,p(X)).
cnf(cnf_negated,axiom,~ p(X) | q(X)).
cnf(cnf_negation,axiom,~ (p(X)) | $false).
%----Annotations
fof(source_list,plain,p,[inference(r,[],[a]),file('f.p')]).
fof(file_info,axiom,p,file('f.p',p_name)).
fof(parents,plain,p,inference(r,[],[a,b])).
fof(theory,axiom,p,theory(equality,[symmetry])).
fof(creator,axiom,p,creator(prover,[],[a])).
fof(parent_details,plain,p,inference(r,[],[a:[b]])).
fof(general_terms,plain,p,inference(r,[bind(X,a):b,[c,d],[]],[a])).
fof(formula_data,plain,p,inference(r,[$thf(p @ a),$tff(p(a)),$fof(p(a)),$cnf(p(a) | q),$fot(f(a))],[a])).
fof(introduced,plain,p,introduced(definition,[new_symbols(def,[q])],[])).
fof(unknown_source,plain,p,unknown).
fof(role_refined,hypothesis-level(2),p).
%----Includes
include('Axioms/SET003+0.ax',*).
include('Axioms/SET003+0.ax',[a,17],space).
