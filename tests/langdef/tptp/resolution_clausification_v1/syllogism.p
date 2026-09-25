fof(all_men_are_mortal,axiom,![X]:(man(X) => mortal(X))).
fof(someone_is_a_man,axiom,?[Y]:man(Y)).
cnf(socrates_is_a_man,axiom,man(socrates)).
fof(mortals_die,axiom,![X]:(mortal(X) => dies(X))).
fof(someone_dies,conjecture,?[Z]:dies(Z)).
