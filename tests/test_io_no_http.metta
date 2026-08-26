!(import! &self io)

!(assertEqual (io:capabilities) ())
!(assertEqual
   (http:get "http://127.0.0.1/")
   (Error
     (io:submit (http:request "GET" "http://127.0.0.1/" () "" 30000 8388608))
     "cetta built without HTTP support (rebuild with ENABLE_HTTP=1)"))
