; Without clingo loadable the clingo library's heads are not its to answer:
; they stay as they are written, also under the asp library's names.
!(import! &self clingo)
!(clingo:ground "a :- not b.")
!(clingo:models ((rule a () ())))
!(clingo:solve ((rule a () ())))
!(import-as! &self asp clingo)
!(asp:models ((rule a () ())))
