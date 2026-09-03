!(let* (
    (() (pragma! search-table-mode variant))
    (() (add-atom &self (= (cache-probe nodup) one)))
    (() (assertEqualToResult (cache-probe nodup) (one)))
    (() (add-atom-nodup &self (= (cache-probe nodup) one)))
    (() (assertEqualToResult (cache-probe nodup) (one)))
  )
  ())
