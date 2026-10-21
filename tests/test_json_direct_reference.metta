!(import! &self json)
!(json:inspect-reference "{\"x\":1,\"x\":2}")
!(json:parse-reference "[true,false,null,\"λ\"]")
!(json:inspect-stringify-reference (json:inspect "{\"x\":1,\"x\":2}"))
