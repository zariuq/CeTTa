!(import! &self json)

; Invalid historical number text is rejected by authored-grammar round trip.
!(json-stringify (JsonNumber "01"))
