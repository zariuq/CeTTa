!(let* (
    (() (pragma! search-table-mode variant))
    (() (add-atom &self (= (cache-probe add) one)))
    (() (assertEqualToResult (cache-probe add) (one)))
    (() (add-atom &self (= (cache-probe add) two)))
    (() (assertEqual (collapse (cache-probe add)) (one two)))
  )
  ())
