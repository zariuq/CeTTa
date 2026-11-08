!(import! &self (library lib_import))
!(import_prolog_functions_from_module
    "petta_document_native_v1.pl"
    (gslt2parse-petta-document-native-v1))

; An argument typed Atom is passed as data, unevaluated; %Undefined% is
; evaluated first.
(: gslt2parse-petta-document-native-v1
   (-> %Undefined% %Undefined% %Undefined% %Undefined% String %Undefined%
       Atom Expression))
