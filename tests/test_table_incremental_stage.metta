!(let* (
    (() (pragma! search-table-mode variant))
    (() (add-atom &self (= (stage-probe $x) (pair $x left))))
    (() (add-atom &self (= (stage-probe $x) (pair $x right))))
    (() (assertEqualToResult (stage-probe seed) ((pair seed left) (pair seed right))))
  )
  ())
