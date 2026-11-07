; Without clingo the asp library's clingo heads are not its to answer: they
; stay as they are written.
!(import! &self asp)
!(asp:ground "a :- not b.")
!(asp:clingo-models ((rule a () ())))
