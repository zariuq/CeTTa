; Imported as sat, the ipasir library answers sat:solve and sat:models in the
; sat library's shapes, with the loaded solver as the evidence.
!(import-as! &self sat ipasir)
!(let (unsat (reference $sig)) (sat:solve ((clause (x) ()) (clause () (x)))) reference)
!(sat:solve ((clause (p) ()) (clause (q) (p))))
!(let (models $ms (reference $sig)) (sat:models ((clause (a b) ()) (clause () (a b)))) (size-atom $ms))
