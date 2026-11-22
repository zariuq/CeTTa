!(import! &self ../../lib/petta/lib_he)
(: constructor-host-box (-> Atom %Undefined%))
(= (constructor-host-box $value) (unify $value (Box $field) $field missing))

; An unavailable spelling remains data through an explicit force.
!(constructor-host-box (Box (process_metta_string "(+ 1 2)")))
!(constructor-host-box (Box (tabled (constructor-host-pending 1))))

; Importing the host capability changes how that same spelling executes.
!(import_prolog_function process_metta_string)
!(constructor-host-box (Box (process_metta_string "(= (constructor-host-added) 31)")))
!(constructor-host-added)
!(import! &self (library lib_tabling))
!(constructor-host-box (Box (tabled (constructor-host-pending 1))))
