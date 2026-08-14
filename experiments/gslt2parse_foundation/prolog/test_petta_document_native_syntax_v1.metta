!(import! &self "petta_document_native_v1")

(= (petta-document-native-outcome-tag (completed $Result)) completed)

!(petta-document-native-outcome-tag
    (gslt2parse-petta-document-native-v1
        (argv 1)
        (argv 2)
        (argv 3)
        (argv 4)
        "(a)"
        (argv 5)
        (petta_document_limits
            65536 2000000 20000000 2000000
            10000000 50000000 4096 1000000)))
