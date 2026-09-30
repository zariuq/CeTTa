; A root Undefined type is a wildcard. Nested Undefined is ordinary syntax
; once the interpreter delegates to atom matching.
(: pcg-box-unknown (Box %Undefined%))
(: pcg-box-identity (-> (Box %Undefined%) Atom))
(= (pcg-box-identity $x) $x)
!(pcg-box-identity pcg-box-unknown)

(: pcg-number-box (-> (Box Number) Atom))
(= (pcg-number-box $x) $x)
!(pcg-number-box pcg-box-unknown)
