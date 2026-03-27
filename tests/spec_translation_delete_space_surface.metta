; Audit: delete-space lifecycle surface after a populated named space.

!(bind! &bag (new-space))
!(add-atom &bag doomed)
!(delete-space &bag)
!(get-atoms &bag)
