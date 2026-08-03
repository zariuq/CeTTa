!(add-atom &tx (counter 1))

!(transaction
   (match &tx (counter $n)
     ((remove-atom &tx (counter $n))
      (empty))))

!(collapse (get-atoms &tx))
