!(let* (
    (() (pragma! search-table-mode variant))
    (() (add-atom &self (= (cache-probe remove) one)))
    (() (add-atom &self (= (cache-probe remove) two)))
    (() (assertEqual (collapse (cache-probe remove)) (one two)))
    (() (remove-atom &self (= (cache-probe remove) one)))
    (() (assertEqualToResult (cache-probe remove) (two)))
  )
  ())
