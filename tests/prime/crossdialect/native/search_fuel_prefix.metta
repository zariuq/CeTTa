; Native checking consumes fuel too. A larger budget extends the actual
; retained proof prefix; each retained term must still pass native checking.
!(import! &self search_observation.metta)
(: FuelA (u 0))
(: FuelB (u 0))
(: FuelC (u 0))
(: fuel-a1 FuelA)
(: fuel-a2 FuelA)
(: fuel-b FuelB)
(: FuelRule (-> FuelA FuelB FuelC))
(chaining-rule FuelRule)
!(native-search:observe &self (search-inhabitants &self FuelC 1 3500 2))
!(native-search:observe &self (search-inhabitants &self FuelC 1 10000 2))
