(: ShownBox (-> Number ShownRange))
(: (ShownBox "wrong-field") AlternateRange)
(: shown-body (-> ShownRange))
(= (shown-body) (ShownBox "wrong-field"))

!(get-type (ShownBox "wrong-field"))
!(ShownBox "wrong-field")
!(shown-body)
