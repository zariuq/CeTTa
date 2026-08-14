; Audit: delete-space lifecycle syntax after a populated named space.

!(bind! &bag (new-space))
!(add-atom &bag doomed)
!(let $_ (delete-space &bag) delete-space-returned)
!(get-atoms &bag)
