!(import! &self json)
!(json-stringify (json-parse "{\"a\":true,\"b\":false,\"c\":[true,null]}"))
!(json-stringify (JsonObject ((JsonPair "t" (JsonBool True)) (JsonPair "f" (JsonBool False)))))
!(json:stringify (json:parse "[true,false]"))
