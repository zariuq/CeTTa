% A period in a line comment is not a record boundary.
/* Nor is a period in a block comment. */
fof(decimal_token,axiom,$less(1.25,2.5)).
fof(quoted_tokens,axiom,p('name.with.periods',"object.with.periods")).
include('Axioms/SET003+0.ax',[decimal_token]).
