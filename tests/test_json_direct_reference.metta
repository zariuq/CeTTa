!(import! &self json)
!(json:parse-reference "{\"x\":1,\"x\":2}")
!(json:parse-legacy-reference "[true,false,null,\"λ\"]")
!(json:stringify-reference (json:parse "{\"x\":1,\"x\":2}"))
