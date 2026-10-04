!(import! &self io)

!(assertEqual (io:capabilities) ())
!(assertEqual
   (http:get "http://127.0.0.1/")
   (Error
     (io:submit)
     "cetta built without HTTP support (rebuild with ENABLE_HTTP=1)"))
