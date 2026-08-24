; Native typed-path learning for Hopper Table-1 encryption.
; Search constructs ordinary proof-relevant `hyp` values; List
; lifting then checks the learned relation against every source case.

!(import! &self ../../lib/ilp/prime_native_mil_list.metta)
!(import! &self ../../lib/ilp/hopper_table1_higher_order_types.metta)

(: hopper:encryption:sort (u 0))
(: hopper:encryption:char-sort hopper:encryption:sort)
(: hopper:encryption:num-sort hopper:encryption:sort)
(: hopper:encryption:primitive
  (-> hopper:encryption:sort
      (-> hopper:encryption:sort (u 1))))
(: hopper:encryption:encode-symbol
  (hopper:encryption:primitive
    hopper:encryption:char-sort hopper:encryption:num-sort))
(: hopper:encryption:predecessor-symbol
  (hopper:encryption:primitive
    hopper:encryption:num-sort hopper:encryption:num-sort))
(: hopper:encryption:decode-symbol
  (hopper:encryption:primitive
    hopper:encryption:num-sort hopper:encryption:char-sort))

(= (hyp:carrier hopper:encryption:char-sort) hopper:char)
(= (hyp:carrier hopper:encryption:num-sort) hopper:cipher-num)
(= (hyp:meaning hopper:encryption:char-sort
     hopper:encryption:num-sort hopper:encryption:encode-symbol)
   hopper:cipher:char-to-num)
(= (hyp:meaning hopper:encryption:num-sort
     hopper:encryption:num-sort hopper:encryption:predecessor-symbol)
   hopper:cipher:pred)
(= (hyp:meaning hopper:encryption:num-sort
     hopper:encryption:char-sort hopper:encryption:decode-symbol)
   hopper:cipher:num-to-char)

(= (rel:apply hopper:cipher:char-to-num hopper:char:a) (rel:edge hopper:cipher-num:n0 hopper:cipher:proof:char-to-num-a))
(= (rel:apply hopper:cipher:char-to-num hopper:char:b) (rel:edge hopper:cipher-num:n1 hopper:cipher:proof:char-to-num-b))
(= (rel:apply hopper:cipher:char-to-num hopper:char:c) (rel:edge hopper:cipher-num:n2 hopper:cipher:proof:char-to-num-c))
(= (rel:apply hopper:cipher:char-to-num hopper:char:d) (rel:edge hopper:cipher-num:n3 hopper:cipher:proof:char-to-num-d))
(= (rel:apply hopper:cipher:char-to-num hopper:char:e) (rel:edge hopper:cipher-num:n4 hopper:cipher:proof:char-to-num-e))
(= (rel:apply hopper:cipher:char-to-num hopper:char:f) (rel:edge hopper:cipher-num:n5 hopper:cipher:proof:char-to-num-f))
(= (rel:apply hopper:cipher:char-to-num hopper:char:g) (rel:edge hopper:cipher-num:n6 hopper:cipher:proof:char-to-num-g))
(= (rel:apply hopper:cipher:char-to-num hopper:char:h) (rel:edge hopper:cipher-num:n7 hopper:cipher:proof:char-to-num-h))
(= (rel:apply hopper:cipher:char-to-num hopper:char:i) (rel:edge hopper:cipher-num:n8 hopper:cipher:proof:char-to-num-i))
(= (rel:apply hopper:cipher:char-to-num hopper:char:j) (rel:edge hopper:cipher-num:n9 hopper:cipher:proof:char-to-num-j))
(= (rel:apply hopper:cipher:char-to-num hopper:char:k) (rel:edge hopper:cipher-num:n10 hopper:cipher:proof:char-to-num-k))
(= (rel:apply hopper:cipher:char-to-num hopper:char:l) (rel:edge hopper:cipher-num:n11 hopper:cipher:proof:char-to-num-l))
(= (rel:apply hopper:cipher:char-to-num hopper:char:m) (rel:edge hopper:cipher-num:n12 hopper:cipher:proof:char-to-num-m))
(= (rel:apply hopper:cipher:char-to-num hopper:char:n) (rel:edge hopper:cipher-num:n13 hopper:cipher:proof:char-to-num-n))
(= (rel:apply hopper:cipher:char-to-num hopper:char:o) (rel:edge hopper:cipher-num:n14 hopper:cipher:proof:char-to-num-o))
(= (rel:apply hopper:cipher:char-to-num hopper:char:p) (rel:edge hopper:cipher-num:n15 hopper:cipher:proof:char-to-num-p))
(= (rel:apply hopper:cipher:char-to-num hopper:char:q) (rel:edge hopper:cipher-num:n16 hopper:cipher:proof:char-to-num-q))
(= (rel:apply hopper:cipher:char-to-num hopper:char:r) (rel:edge hopper:cipher-num:n17 hopper:cipher:proof:char-to-num-r))
(= (rel:apply hopper:cipher:char-to-num hopper:char:s) (rel:edge hopper:cipher-num:n18 hopper:cipher:proof:char-to-num-s))
(= (rel:apply hopper:cipher:char-to-num hopper:char:t) (rel:edge hopper:cipher-num:n19 hopper:cipher:proof:char-to-num-t))
(= (rel:apply hopper:cipher:char-to-num hopper:char:u) (rel:edge hopper:cipher-num:n20 hopper:cipher:proof:char-to-num-u))
(= (rel:apply hopper:cipher:char-to-num hopper:char:v) (rel:edge hopper:cipher-num:n21 hopper:cipher:proof:char-to-num-v))
(= (rel:apply hopper:cipher:char-to-num hopper:char:w) (rel:edge hopper:cipher-num:n22 hopper:cipher:proof:char-to-num-w))
(= (rel:apply hopper:cipher:char-to-num hopper:char:x) (rel:edge hopper:cipher-num:n23 hopper:cipher:proof:char-to-num-x))
(= (rel:apply hopper:cipher:char-to-num hopper:char:y) (rel:edge hopper:cipher-num:n24 hopper:cipher:proof:char-to-num-y))
(= (rel:apply hopper:cipher:char-to-num hopper:char:z) (rel:edge hopper:cipher-num:n25 hopper:cipher:proof:char-to-num-z))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n0) (rel:edge hopper:cipher-num:n25 hopper:cipher:proof:pred-0-25))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n1) (rel:edge hopper:cipher-num:n0 hopper:cipher:proof:pred-1-0))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n2) (rel:edge hopper:cipher-num:n1 hopper:cipher:proof:pred-2-1))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n3) (rel:edge hopper:cipher-num:n2 hopper:cipher:proof:pred-3-2))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n4) (rel:edge hopper:cipher-num:n3 hopper:cipher:proof:pred-4-3))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n5) (rel:edge hopper:cipher-num:n4 hopper:cipher:proof:pred-5-4))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n6) (rel:edge hopper:cipher-num:n5 hopper:cipher:proof:pred-6-5))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n7) (rel:edge hopper:cipher-num:n6 hopper:cipher:proof:pred-7-6))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n8) (rel:edge hopper:cipher-num:n7 hopper:cipher:proof:pred-8-7))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n9) (rel:edge hopper:cipher-num:n8 hopper:cipher:proof:pred-9-8))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n10) (rel:edge hopper:cipher-num:n9 hopper:cipher:proof:pred-10-9))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n11) (rel:edge hopper:cipher-num:n10 hopper:cipher:proof:pred-11-10))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n12) (rel:edge hopper:cipher-num:n11 hopper:cipher:proof:pred-12-11))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n13) (rel:edge hopper:cipher-num:n12 hopper:cipher:proof:pred-13-12))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n14) (rel:edge hopper:cipher-num:n13 hopper:cipher:proof:pred-14-13))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n15) (rel:edge hopper:cipher-num:n14 hopper:cipher:proof:pred-15-14))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n16) (rel:edge hopper:cipher-num:n15 hopper:cipher:proof:pred-16-15))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n17) (rel:edge hopper:cipher-num:n16 hopper:cipher:proof:pred-17-16))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n18) (rel:edge hopper:cipher-num:n17 hopper:cipher:proof:pred-18-17))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n19) (rel:edge hopper:cipher-num:n18 hopper:cipher:proof:pred-19-18))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n20) (rel:edge hopper:cipher-num:n19 hopper:cipher:proof:pred-20-19))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n21) (rel:edge hopper:cipher-num:n20 hopper:cipher:proof:pred-21-20))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n22) (rel:edge hopper:cipher-num:n21 hopper:cipher:proof:pred-22-21))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n23) (rel:edge hopper:cipher-num:n22 hopper:cipher:proof:pred-23-22))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n24) (rel:edge hopper:cipher-num:n23 hopper:cipher:proof:pred-24-23))
(= (rel:apply hopper:cipher:pred hopper:cipher-num:n25) (rel:edge hopper:cipher-num:n24 hopper:cipher:proof:pred-25-24))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n0) (rel:edge hopper:char:a hopper:cipher:proof:num-to-char-a))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n1) (rel:edge hopper:char:b hopper:cipher:proof:num-to-char-b))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n2) (rel:edge hopper:char:c hopper:cipher:proof:num-to-char-c))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n3) (rel:edge hopper:char:d hopper:cipher:proof:num-to-char-d))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n4) (rel:edge hopper:char:e hopper:cipher:proof:num-to-char-e))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n5) (rel:edge hopper:char:f hopper:cipher:proof:num-to-char-f))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n6) (rel:edge hopper:char:g hopper:cipher:proof:num-to-char-g))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n7) (rel:edge hopper:char:h hopper:cipher:proof:num-to-char-h))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n8) (rel:edge hopper:char:i hopper:cipher:proof:num-to-char-i))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n9) (rel:edge hopper:char:j hopper:cipher:proof:num-to-char-j))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n10) (rel:edge hopper:char:k hopper:cipher:proof:num-to-char-k))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n11) (rel:edge hopper:char:l hopper:cipher:proof:num-to-char-l))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n12) (rel:edge hopper:char:m hopper:cipher:proof:num-to-char-m))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n13) (rel:edge hopper:char:n hopper:cipher:proof:num-to-char-n))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n14) (rel:edge hopper:char:o hopper:cipher:proof:num-to-char-o))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n15) (rel:edge hopper:char:p hopper:cipher:proof:num-to-char-p))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n16) (rel:edge hopper:char:q hopper:cipher:proof:num-to-char-q))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n17) (rel:edge hopper:char:r hopper:cipher:proof:num-to-char-r))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n18) (rel:edge hopper:char:s hopper:cipher:proof:num-to-char-s))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n19) (rel:edge hopper:char:t hopper:cipher:proof:num-to-char-t))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n20) (rel:edge hopper:char:u hopper:cipher:proof:num-to-char-u))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n21) (rel:edge hopper:char:v hopper:cipher:proof:num-to-char-v))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n22) (rel:edge hopper:char:w hopper:cipher:proof:num-to-char-w))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n23) (rel:edge hopper:char:x hopper:cipher:proof:num-to-char-x))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n24) (rel:edge hopper:char:y hopper:cipher:proof:num-to-char-y))
(= (rel:apply hopper:cipher:num-to-char hopper:cipher-num:n25) (rel:edge hopper:char:z hopper:cipher:proof:num-to-char-z))

(= (hopper:encryption:path-shape)
   (hyp:path:more
     (hyp:path:more
       (hyp:path:more hyp:path:one))))

(= (hopper:encryption:scalar-examples)
   (hyp:examples
     ((hyp:example hopper:char:a hopper:char:y))
     ((hyp:example hopper:char:a hopper:char:a))))

(= (hopper:encryption:list-examples)
   (rel:list-examples
     (
       (rel:list-example (hopper:char:w hopper:char:h hopper:char:r hopper:char:e hopper:char:b hopper:char:t hopper:char:w hopper:char:t hopper:char:g) (hopper:char:u hopper:char:f hopper:char:p hopper:char:c hopper:char:z hopper:char:r hopper:char:u hopper:char:r hopper:char:e))
       (rel:list-example (hopper:char:u hopper:char:n) (hopper:char:s hopper:char:l))
       (rel:list-example (hopper:char:g hopper:char:m hopper:char:m hopper:char:z hopper:char:q hopper:char:y) (hopper:char:e hopper:char:k hopper:char:k hopper:char:x hopper:char:o hopper:char:w))
       (rel:list-example (hopper:char:v hopper:char:g hopper:char:a hopper:char:j hopper:char:n hopper:char:t hopper:char:p hopper:char:v hopper:char:r hopper:char:e hopper:char:y hopper:char:v hopper:char:y) (hopper:char:t hopper:char:e hopper:char:y hopper:char:h hopper:char:l hopper:char:r hopper:char:n hopper:char:t hopper:char:p hopper:char:c hopper:char:w hopper:char:t hopper:char:w))
       (rel:list-example (hopper:char:t) (hopper:char:r))
       (rel:list-example (hopper:char:d hopper:char:k hopper:char:n hopper:char:e hopper:char:v) (hopper:char:b hopper:char:i hopper:char:l hopper:char:c hopper:char:t))
       (rel:list-example (hopper:char:m hopper:char:o hopper:char:z hopper:char:t hopper:char:w hopper:char:y hopper:char:u hopper:char:a hopper:char:k hopper:char:y hopper:char:h hopper:char:k hopper:char:k hopper:char:k hopper:char:a hopper:char:j hopper:char:p) (hopper:char:k hopper:char:m hopper:char:x hopper:char:r hopper:char:u hopper:char:w hopper:char:s hopper:char:y hopper:char:i hopper:char:w hopper:char:f hopper:char:i hopper:char:i hopper:char:i hopper:char:y hopper:char:h hopper:char:n))
       (rel:list-example (hopper:char:c hopper:char:x hopper:char:s hopper:char:o hopper:char:b hopper:char:h hopper:char:d hopper:char:i hopper:char:r hopper:char:s) (hopper:char:a hopper:char:v hopper:char:q hopper:char:m hopper:char:z hopper:char:f hopper:char:b hopper:char:g hopper:char:p hopper:char:q))
       (rel:list-example (hopper:char:y hopper:char:k hopper:char:h hopper:char:i hopper:char:z hopper:char:i hopper:char:s hopper:char:e hopper:char:z hopper:char:v hopper:char:f hopper:char:s hopper:char:i hopper:char:y hopper:char:c hopper:char:q) (hopper:char:w hopper:char:i hopper:char:f hopper:char:g hopper:char:x hopper:char:g hopper:char:q hopper:char:c hopper:char:x hopper:char:t hopper:char:d hopper:char:q hopper:char:g hopper:char:w hopper:char:a hopper:char:o))
       (rel:list-example (hopper:char:k hopper:char:t hopper:char:q hopper:char:s hopper:char:g hopper:char:b hopper:char:d hopper:char:c hopper:char:b hopper:char:e hopper:char:b hopper:char:r hopper:char:s hopper:char:n hopper:char:l hopper:char:t hopper:char:f hopper:char:d hopper:char:e) (hopper:char:i hopper:char:r hopper:char:o hopper:char:q hopper:char:e hopper:char:z hopper:char:b hopper:char:a hopper:char:z hopper:char:c hopper:char:z hopper:char:p hopper:char:q hopper:char:l hopper:char:j hopper:char:r hopper:char:d hopper:char:b hopper:char:c))
       (rel:list-example (hopper:char:x hopper:char:c hopper:char:b hopper:char:p hopper:char:k hopper:char:l hopper:char:w hopper:char:s hopper:char:p hopper:char:x hopper:char:j hopper:char:n hopper:char:b hopper:char:x hopper:char:n hopper:char:z hopper:char:f hopper:char:e) (hopper:char:v hopper:char:a hopper:char:z hopper:char:n hopper:char:i hopper:char:j hopper:char:u hopper:char:q hopper:char:n hopper:char:v hopper:char:h hopper:char:l hopper:char:z hopper:char:v hopper:char:l hopper:char:x hopper:char:d hopper:char:c))
       (rel:list-example (hopper:char:n) (hopper:char:l))
       (rel:list-example (hopper:char:q hopper:char:t) (hopper:char:o hopper:char:r))
       (rel:list-example (hopper:char:v hopper:char:q hopper:char:i hopper:char:f hopper:char:o hopper:char:s hopper:char:k hopper:char:w hopper:char:v hopper:char:f) (hopper:char:t hopper:char:o hopper:char:g hopper:char:d hopper:char:m hopper:char:q hopper:char:i hopper:char:u hopper:char:t hopper:char:d))
       (rel:list-example (hopper:char:y hopper:char:j hopper:char:n hopper:char:d hopper:char:x hopper:char:q hopper:char:e hopper:char:r hopper:char:j hopper:char:g hopper:char:o hopper:char:t hopper:char:c hopper:char:x) (hopper:char:w hopper:char:h hopper:char:l hopper:char:b hopper:char:v hopper:char:o hopper:char:c hopper:char:p hopper:char:h hopper:char:e hopper:char:m hopper:char:r hopper:char:a hopper:char:v))
       (rel:list-example (hopper:char:f hopper:char:q hopper:char:d hopper:char:f hopper:char:v hopper:char:c hopper:char:o hopper:char:r hopper:char:z hopper:char:q hopper:char:y hopper:char:z hopper:char:x hopper:char:v hopper:char:y) (hopper:char:d hopper:char:o hopper:char:b hopper:char:d hopper:char:t hopper:char:a hopper:char:m hopper:char:p hopper:char:x hopper:char:o hopper:char:w hopper:char:x hopper:char:v hopper:char:t hopper:char:w))
       (rel:list-example (hopper:char:f hopper:char:h hopper:char:e hopper:char:m hopper:char:r hopper:char:s hopper:char:x hopper:char:q hopper:char:m hopper:char:v hopper:char:j hopper:char:l) (hopper:char:d hopper:char:f hopper:char:c hopper:char:k hopper:char:p hopper:char:q hopper:char:v hopper:char:o hopper:char:k hopper:char:t hopper:char:h hopper:char:j))
       (rel:list-example (hopper:char:j hopper:char:k hopper:char:r hopper:char:l hopper:char:o hopper:char:x) (hopper:char:h hopper:char:i hopper:char:p hopper:char:j hopper:char:m hopper:char:v))
       (rel:list-example (hopper:char:s hopper:char:j hopper:char:y hopper:char:g hopper:char:c hopper:char:x hopper:char:p hopper:char:b) (hopper:char:q hopper:char:h hopper:char:w hopper:char:e hopper:char:a hopper:char:v hopper:char:n hopper:char:z))
       (rel:list-example (hopper:char:h hopper:char:w hopper:char:y hopper:char:y hopper:char:a hopper:char:h hopper:char:v hopper:char:t hopper:char:r hopper:char:l hopper:char:e hopper:char:b hopper:char:y hopper:char:b hopper:char:r hopper:char:l hopper:char:s hopper:char:z hopper:char:k) (hopper:char:f hopper:char:u hopper:char:w hopper:char:w hopper:char:y hopper:char:f hopper:char:t hopper:char:r hopper:char:p hopper:char:j hopper:char:c hopper:char:z hopper:char:w hopper:char:z hopper:char:p hopper:char:j hopper:char:q hopper:char:x hopper:char:i))
       (rel:list-example (hopper:char:u hopper:char:y hopper:char:w hopper:char:y hopper:char:e hopper:char:z hopper:char:e hopper:char:e hopper:char:z hopper:char:p hopper:char:i hopper:char:k hopper:char:t hopper:char:w hopper:char:w hopper:char:o hopper:char:b hopper:char:i hopper:char:r hopper:char:r) (hopper:char:s hopper:char:w hopper:char:u hopper:char:w hopper:char:c hopper:char:x hopper:char:c hopper:char:c hopper:char:x hopper:char:n hopper:char:g hopper:char:i hopper:char:r hopper:char:u hopper:char:u hopper:char:m hopper:char:z hopper:char:g hopper:char:p hopper:char:p))
       (rel:list-example (hopper:char:u hopper:char:x hopper:char:c hopper:char:c hopper:char:y) (hopper:char:s hopper:char:v hopper:char:a hopper:char:a hopper:char:w))
       (rel:list-example (hopper:char:u hopper:char:j hopper:char:m hopper:char:b hopper:char:m hopper:char:k hopper:char:g hopper:char:m hopper:char:m hopper:char:d hopper:char:e hopper:char:k hopper:char:j hopper:char:v hopper:char:d hopper:char:d) (hopper:char:s hopper:char:h hopper:char:k hopper:char:z hopper:char:k hopper:char:i hopper:char:e hopper:char:k hopper:char:k hopper:char:b hopper:char:c hopper:char:i hopper:char:h hopper:char:t hopper:char:b hopper:char:b))
       (rel:list-example (hopper:char:i hopper:char:i hopper:char:h hopper:char:b hopper:char:p hopper:char:p) (hopper:char:g hopper:char:g hopper:char:f hopper:char:z hopper:char:n hopper:char:n))
       (rel:list-example (hopper:char:e hopper:char:d hopper:char:e hopper:char:s hopper:char:a hopper:char:z hopper:char:q hopper:char:i hopper:char:e hopper:char:x hopper:char:l hopper:char:r hopper:char:f hopper:char:b hopper:char:w hopper:char:k hopper:char:f hopper:char:b) (hopper:char:c hopper:char:b hopper:char:c hopper:char:q hopper:char:y hopper:char:x hopper:char:o hopper:char:g hopper:char:c hopper:char:v hopper:char:j hopper:char:p hopper:char:d hopper:char:z hopper:char:u hopper:char:i hopper:char:d hopper:char:z))
       (rel:list-example (hopper:char:l hopper:char:m hopper:char:b hopper:char:y hopper:char:y hopper:char:y hopper:char:s hopper:char:g hopper:char:n hopper:char:k) (hopper:char:j hopper:char:k hopper:char:z hopper:char:w hopper:char:w hopper:char:w hopper:char:q hopper:char:e hopper:char:l hopper:char:i))
       (rel:list-example (hopper:char:a hopper:char:p hopper:char:w hopper:char:c hopper:char:j hopper:char:k hopper:char:g hopper:char:y hopper:char:e hopper:char:w hopper:char:f hopper:char:z hopper:char:t hopper:char:k) (hopper:char:y hopper:char:n hopper:char:u hopper:char:a hopper:char:h hopper:char:i hopper:char:e hopper:char:w hopper:char:c hopper:char:u hopper:char:d hopper:char:x hopper:char:r hopper:char:i))
       (rel:list-example (hopper:char:b) (hopper:char:z))
     )
     (
       (rel:list-example (hopper:char:t hopper:char:q) (hopper:char:w hopper:char:t))
       (rel:list-example (hopper:char:m hopper:char:i hopper:char:b hopper:char:g hopper:char:k hopper:char:m hopper:char:m hopper:char:y hopper:char:p hopper:char:o hopper:char:x hopper:char:y hopper:char:h hopper:char:g hopper:char:p hopper:char:i hopper:char:h) (hopper:char:o hopper:char:k hopper:char:d hopper:char:i hopper:char:m hopper:char:o hopper:char:o hopper:char:a hopper:char:r hopper:char:q hopper:char:z hopper:char:a hopper:char:j hopper:char:i hopper:char:r hopper:char:k hopper:char:j))
       (rel:list-example (hopper:char:n hopper:char:e hopper:char:e hopper:char:h hopper:char:i hopper:char:b hopper:char:r hopper:char:x hopper:char:q hopper:char:v hopper:char:y hopper:char:x hopper:char:z hopper:char:e hopper:char:r) (hopper:char:x hopper:char:o hopper:char:o hopper:char:r hopper:char:s hopper:char:l hopper:char:b hopper:char:h hopper:char:a hopper:char:f hopper:char:i hopper:char:h hopper:char:j hopper:char:o hopper:char:b))
       (rel:list-example (hopper:char:s hopper:char:k hopper:char:i hopper:char:d hopper:char:y hopper:char:o hopper:char:z hopper:char:b hopper:char:m) (hopper:char:g hopper:char:y hopper:char:w hopper:char:r hopper:char:m hopper:char:c hopper:char:n hopper:char:p hopper:char:a))
       (rel:list-example (hopper:char:d hopper:char:f hopper:char:s hopper:char:l hopper:char:n hopper:char:w hopper:char:h hopper:char:f hopper:char:p hopper:char:j hopper:char:i hopper:char:o hopper:char:i hopper:char:d hopper:char:s hopper:char:v hopper:char:n hopper:char:w hopper:char:u) (hopper:char:p hopper:char:r hopper:char:e hopper:char:x hopper:char:z hopper:char:i hopper:char:t hopper:char:r hopper:char:b hopper:char:v hopper:char:u hopper:char:a hopper:char:u hopper:char:p hopper:char:e hopper:char:h hopper:char:z hopper:char:i hopper:char:g))
       (rel:list-example (hopper:char:r hopper:char:r hopper:char:h hopper:char:x hopper:char:m hopper:char:o hopper:char:n hopper:char:z hopper:char:w hopper:char:y hopper:char:u hopper:char:u hopper:char:z) (hopper:char:x hopper:char:x hopper:char:n hopper:char:d hopper:char:s hopper:char:u hopper:char:t hopper:char:f hopper:char:c hopper:char:e hopper:char:a hopper:char:a hopper:char:f))
       (rel:list-example (hopper:char:o hopper:char:t hopper:char:u hopper:char:b hopper:char:m hopper:char:v hopper:char:m hopper:char:w hopper:char:x hopper:char:l hopper:char:k hopper:char:v hopper:char:w hopper:char:g) (hopper:char:g hopper:char:l hopper:char:m hopper:char:t hopper:char:e hopper:char:n hopper:char:e hopper:char:o hopper:char:p hopper:char:d hopper:char:c hopper:char:n hopper:char:o hopper:char:y))
       (rel:list-example (hopper:char:y hopper:char:p hopper:char:z hopper:char:k hopper:char:a hopper:char:r hopper:char:k hopper:char:p hopper:char:l hopper:char:c hopper:char:q) (hopper:char:j hopper:char:a hopper:char:k hopper:char:v hopper:char:l hopper:char:c hopper:char:v hopper:char:a hopper:char:w hopper:char:n hopper:char:b))
       (rel:list-example (hopper:char:y hopper:char:x hopper:char:x hopper:char:o) (hopper:char:i hopper:char:h hopper:char:h hopper:char:y))
       (rel:list-example (hopper:char:t hopper:char:r hopper:char:h hopper:char:n hopper:char:g hopper:char:i hopper:char:s hopper:char:j) (hopper:char:b hopper:char:z hopper:char:p hopper:char:v hopper:char:o hopper:char:q hopper:char:a hopper:char:r))
       (rel:list-example (hopper:char:z hopper:char:v hopper:char:x hopper:char:q hopper:char:p hopper:char:f hopper:char:o hopper:char:t hopper:char:y hopper:char:f hopper:char:x hopper:char:x hopper:char:a) (hopper:char:d hopper:char:z hopper:char:b hopper:char:u hopper:char:t hopper:char:j hopper:char:s hopper:char:x hopper:char:c hopper:char:j hopper:char:b hopper:char:b hopper:char:e))
       (rel:list-example (hopper:char:p hopper:char:i) (hopper:char:o hopper:char:h))
       (rel:list-example (hopper:char:p hopper:char:i) (hopper:char:p hopper:char:i))
     )))

(= (hopper:encryption:learn)
   (hyp:learn-path
     &self hopper:encryption:sort hopper:encryption:primitive
     hopper:encryption:char-sort hopper:encryption:char-sort
     (hopper:encryption:path-shape)
     (hopper:encryption:scalar-examples)))

!(list:len
   (collapse
     (map-rel:run hopper:cipher:char-to-num
       (hopper:char:a))))

!(hyp:path-benchmark
   hopper-encryption-native
   (native-hyp (candidate-bag exact) (proof-relevant True)
     (relator list) (source hopper-table-1-encryption))
   &self hopper:encryption:sort hopper:encryption:primitive
   hopper:encryption:char-sort hopper:encryption:char-sort
   (hopper:encryption:path-shape)
   (hopper:encryption:scalar-examples))

!(hopper:encryption:learn)

!(let $program (hopper:encryption:learn)
   (rel:list-consistent
     (hyp:relation $program)
     (hopper:encryption:list-examples)))

!(let $program (hopper:encryption:learn)
   (list:len
     (collapse
       (map-rel:run (hyp:relation $program)
         (hopper:char:a)))))
