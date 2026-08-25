!(import! &self io)

!(assertEqual (io:capabilities) (http))
!(assertEqual (io:poll) (io:idle))
!(assertEqual
   (io:submit (http:request "GET" "file:///etc/passwd" () "" 1000 1024))
   (Error
     (io:submit (http:request "GET" "file:///etc/passwd" () "" 1000 1024))
     "only http and https URLs are supported"))
!(assertEqual
   (io:submit
     (http:request "GET" "http://127.0.0.1/"
       ((http:header "X-Test" "bad\nvalue")) "" 1000 1024))
   (Error
     (io:submit
       (http:request "GET" "http://127.0.0.1/"
         ((http:header "X-Test" "bad\nvalue")) "" 1000 1024))
     "expected (http:header key value) entries without control characters"))
