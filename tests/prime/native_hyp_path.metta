; A length-four hypothesis path remains an ordinary structural `hyp` value.
; Three primitive declarations yield 3^4 raw programs, while shared family
; indices retain exactly the encode/pred/pred/decode path.

!(import! &self ../../lib/ilp/prime_native_mil.metta)

(: path-test:sort (u 0))
(: path-test:char path-test:sort)
(: path-test:num path-test:sort)
(: path-test:primitive
   (-> path-test:sort (-> path-test:sort (u 1))))

(: path-test:encode-symbol
   (path-test:primitive path-test:char path-test:num))
(: path-test:pred-symbol
   (path-test:primitive path-test:num path-test:num))
(: path-test:decode-symbol
   (path-test:primitive path-test:num path-test:char))

(: path-test:encode (-> u0 (-> u0 u1)))
(: path-test:pred (-> u0 (-> u0 u1)))
(: path-test:decode (-> u0 (-> u0 u1)))
(: path-test:a u0)
(: path-test:y u0)
(: path-test:n0 u0)
(: path-test:n25 u0)
(: path-test:n24 u0)
(: path-test:encode-a
   (path-test:encode path-test:a path-test:n0))
(: path-test:pred-0-25
   (path-test:pred path-test:n0 path-test:n25))
(: path-test:pred-25-24
   (path-test:pred path-test:n25 path-test:n24))
(: path-test:decode-24-y
   (path-test:decode path-test:n24 path-test:y))

(= (hyp:carrier path-test:char) u0)
(= (hyp:carrier path-test:num) u0)
(= (hyp:meaning path-test:char path-test:num path-test:encode-symbol)
   path-test:encode)
(= (hyp:meaning path-test:num path-test:num path-test:pred-symbol)
   path-test:pred)
(= (hyp:meaning path-test:num path-test:char path-test:decode-symbol)
   path-test:decode)

(= (rel:apply path-test:encode path-test:a)
   (rel:edge path-test:n0 path-test:encode-a))
(= (rel:apply path-test:pred path-test:n0)
   (rel:edge path-test:n25 path-test:pred-0-25))
(= (rel:apply path-test:pred path-test:n25)
   (rel:edge path-test:n24 path-test:pred-25-24))
(= (rel:apply path-test:decode path-test:n24)
   (rel:edge path-test:y path-test:decode-24-y))

(= (path-test:shape)
   (hyp:path:more
     (hyp:path:more
       (hyp:path:more hyp:path:one))))

(= (path-test:examples)
   (hyp:examples
     ((hyp:example path-test:a path-test:y))
     ((hyp:example path-test:a path-test:a))))

!(list:len
   (hyp:path-candidate-data-untyped
     &self path-test:sort path-test:primitive (path-test:shape)))

!(list:len
   (hyp:path-candidate-data-typed
     &self path-test:sort path-test:primitive
     path-test:char path-test:char (path-test:shape)))

!(hyp:path-benchmark
   path-test
   (native-hyp (candidate-bag exact) (proof-relevant True))
   &self path-test:sort path-test:primitive
   path-test:char path-test:char
   (path-test:shape) (path-test:examples))

!(hyp:learn-path
   &self path-test:sort path-test:primitive
   path-test:char path-test:char
   (path-test:shape) (path-test:examples))

!(let $program
     (hyp:learn-path
       &self path-test:sort path-test:primitive
       path-test:char path-test:char
       (path-test:shape) (path-test:examples))
   (collapse (hyp:run $program path-test:a)))
