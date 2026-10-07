; Bare set: retains the common profile while extending its declaration view.
!(set:define CoreFunction class (= CoreFunction (-> set set)))
!(set:define coreIdentity CoreFunction (= (coreIdentity $x) $x))
!(car-atom (try (set:check coreIdentity (-> set set))))
!(car-atom (try (set:eq coreIdentity (lam (x : set) x))))
!(car-atom (try (set:known-proposition foundationLaw)))
!(car-atom (try (set:known-proposition excludedMiddle)))
