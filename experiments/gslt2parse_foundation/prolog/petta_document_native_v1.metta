!(import! &self (library lib_import))
!(import_prolog_functions_from_module
    "experiments/gslt2parse_foundation/petta_document_native_v1.pl"
    (gslt2parse-petta-document-native-v1))

(: gslt2parse-petta-document-native-v1
   (-> Atom Atom Atom Atom String Atom Expression Expression))
