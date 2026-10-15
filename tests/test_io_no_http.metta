!(import! &self io)

(= (test:io-http-provider $provider) (== $provider http))

!(let $providers (io:capabilities)
   (assertEqual (filter-atom $providers test:io-http-provider) ()))
!(assertEqual
   (http:get "http://127.0.0.1/")
   (Error
     (io:submit)
     "cetta built without HTTP support (rebuild with ENABLE_HTTP=1)"))
