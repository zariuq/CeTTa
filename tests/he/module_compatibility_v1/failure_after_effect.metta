!(import! &store failure_store)
!(add-atom &store (escaped-write))
!(println! "initialization-effect")
(unpublished-fact 7)
(unterminated
