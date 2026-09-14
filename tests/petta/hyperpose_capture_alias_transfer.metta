(= (make-capture-alias)
   (let $capture capture
        (CapturePair $capture $capture)))

(= (capture-alias-survives-hyperpose)
   (let $results
        (collapse
          (hyperpose ((make-capture-alias))))
        (case $results
          ((((CapturePair $same $same)) true)
           ($_ malformed-result)))))

!(test (capture-alias-survives-hyperpose) true)
