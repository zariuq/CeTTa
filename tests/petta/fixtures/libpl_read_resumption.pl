cetta_decode_mixed_answer([plain]).
cetta_decode_mixed_answer([Cycle]) :- Cycle = f(Cycle).
cetta_decode_mixed_answer([last]).
