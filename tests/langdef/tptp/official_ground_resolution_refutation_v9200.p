cnf(p_or_q,axiom,p|q).
cnf(not_p,axiom,~p).
cnf(not_q,axiom,~q).
cnf(q,plain,q,inference(resolution,[status(thm)],[p_or_q,not_p])).
cnf(empty,plain,$false,inference(resolution,[status(thm)],[q,not_q])).
