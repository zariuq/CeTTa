% The syllogism again, with problem symbols spelled as the names an export
% would give its introduced symbols: sk0 for the Skolem constant, df0 and a
% quoted 'df1' for the two definitional predicates, and a quoted 'sk0_1'
% for the Skolem constant's first numbered variant.
fof(all_men_are_mortal,axiom,![X]:(man(X) => mortal(X))).
fof(someone_is_a_man,axiom,?[Y]:man(Y)).
cnf(sk0_is_a_man,axiom,man(sk0)).
cnf(names,axiom,named(df0,'df1','sk0_1')).
fof(mortals_die,axiom,![X]:(mortal(X) => dies(X))).
fof(someone_dies,conjecture,?[Z]:dies(Z)).
