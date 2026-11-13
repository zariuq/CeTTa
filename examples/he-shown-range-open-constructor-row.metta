(: OpenShownBox (-> $t $t OpenShownRange))
(: (OpenShownBox 1 "wrong-field") AlternateRange)
(: shown-open-body (-> OpenShownRange))
(= (shown-open-body) (OpenShownBox 1 "wrong-field"))

!(get-type (OpenShownBox 1 "wrong-field"))
!(shown-open-body)
