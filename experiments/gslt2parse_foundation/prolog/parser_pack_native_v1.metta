!(import! &self (library lib_import))
!(import_prolog_functions_from_module
    "experiments/gslt2parse_foundation/parser_pack_native_v1.pl"
    (gslt2parse-native-v1))

(: gslt2parse-native-v1
   (-> Atom Expression Expression Expression Expression Expression Expression))
