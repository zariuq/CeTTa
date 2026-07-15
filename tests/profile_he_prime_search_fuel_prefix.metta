; More resources may extend, but never replace, the returned proof prefix.
(: fuel-a1 FuelA)
(: fuel-a2 FuelA)
(: fuel-b FuelB)
(: FuelRule (-> FuelA FuelB FuelC))
(chaining-rule FuelRule)
!(type-inhabit &self FuelC 1 65 2)
!(type-inhabit &self FuelC 1 200 2)
