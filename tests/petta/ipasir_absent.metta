; Without an IPASIR solver loadable the ipasir library's heads are not its to
; answer: they stay as they are written, also under the sat library's names.
!(import! &self ipasir)
!(ipasir:solve ((clause (a) ()) (clause (b) (a))))
!(ipasir:models ((clause (a) ())))
!(import-as! &self sat ipasir)
!(sat:solve ((clause (a) ())))
