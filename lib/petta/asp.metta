; Ground answer-set programs; see lib/asp.metta.
(= (asp:solve $program) (__cetta_lib_asp_solve $program))
(= (asp:models $program) (__cetta_lib_asp_models $program))
(= (asp:ground $text) (__cetta_lib_asp_ground $text))
(= (asp:clingo-models $program) (__cetta_lib_asp_clingo_models $program))
(= (asp:trace $program) (__cetta_lib_asp_trace $program))
