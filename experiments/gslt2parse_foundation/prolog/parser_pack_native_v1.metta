!(import! &self (library lib_import))
!(import_prolog_functions_from_module
    "parser_pack_native_v1.pl"
    (gslt2parse-native-v1))

; An argument typed Atom is passed as data, unevaluated; %Undefined% is
; evaluated first.
(: gslt2parse-native-v1
   (-> %Undefined% Atom Atom Atom Atom Atom Expression))
