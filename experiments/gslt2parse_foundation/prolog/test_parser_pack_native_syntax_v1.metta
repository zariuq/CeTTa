!(import! &self "parser_pack_native_v1")

(= (native-outcome-tag (completed $Result)) completed)
(= (native-outcome-tag (unsupported $Reason)) unsupported)

!(let $PresentationRoot (argv 1)
    (native-outcome-tag
        (gslt2parse-native-v1
            gll
            $PresentationRoot
            (paths
                "core/syntax_core_v1.metta"
                "shared/lookahead_core_v1.metta"
                "shared/char_core_v1.metta"
                "languages/megalodon_dynamic_v1.metta")
            (pp-rel mg-name-list)
            (cons (cp 97) nil)
            (native_limits 2000000 4096 65536))))

!(let $PresentationRoot (argv 1)
    (native-outcome-tag
        (gslt2parse-native-v1
            glr
            $PresentationRoot
            (paths
                "core/syntax_core_v1.metta"
                "shared/lookahead_core_v1.metta"
                "shared/ground_relations_v1.metta"
                "shared/cetta_prime_scalar_classes_v1.metta"
                "languages/cetta_prime_reader_v1.metta")
            (pp-def cetta-prime-file)
            nil
            (native_limits 2000000 4096 65536))))
