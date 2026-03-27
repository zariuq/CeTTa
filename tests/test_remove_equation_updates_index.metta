; Removing an equation must also evict it from the equation index.
!(add-atom &self (= (f) ok))
!(remove-atom &self (= (f) ok))
!(f)
