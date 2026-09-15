% Unicode line-comment canary: ∀ x ∃ y ⇒ x ≥ y.
/* Unicode block-comment canary: λ α β → α × β. */
tpi(tpi_plain,plain,p).
thf(thf_type,type,p:$o).
thf(thf_formula,axiom,![P:$o]:(P=>P)).
thf(nhf_formula,axiom,({$necessary}@p)).
tff(tff_type,type,q:$o).
tff(tff_formula,axiom,![X:$i]:q).
tff(nxf_formula,axiom,({$possible}@(q))).
tcf(tcf_formula,axiom,(p|~q)).
fof(fof_formula,axiom,![X]:(p(X)=>?[Y]:q(Y))).
cnf(cnf_formula,axiom,p(X)|~q(X)).
include('Axioms/SET003+0.ax',[fof_formula,17]).
fof(annotated,plain,p,inference(magic,[status(thm)],[fof_formula])).
