!(import! &self (library lib_he))

(: context-value ContextType)
!(test (get-type-space &self context-value) ContextType)
!(test (evalc (+ 20 22) &self) 42)

!(add-reduct &self (= (stored-reduct) (+ 20 22)))
!(test (match &self (= (stored-reduct) $r) (quote $r)) (42))
!(test (let (Error instantiation_error $source) (catch (match $space atom found)) refused)
       refused)

(: install-from-prefix (-> Atom Atom %Undefined%))
(= (install-from-prefix (cons , $prefix) $head)
   (progn (cut) (add-atom &self (= $head $prefix))))
(= (install-from-prefix $other $head)
   (add-atom &self (= $head forbidden)))
!(install-from-prefix (, (Node $x)) (installed $x))
!(test (collapse (match &self (= (installed tag) ((Node tag))) selected))
       (selected))
