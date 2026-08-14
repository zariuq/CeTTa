(: guarded-expression-head (-[det]-> Atom Bool))
(= (guarded-expression-head $value)
   (if (is-expr $value)
      (== (car-atom $value) marker)
      false))

(: unguarded-expression-head (-[det]-> Atom Bool))
(= (unguarded-expression-head $value)
   (== (car-atom $value) marker))
