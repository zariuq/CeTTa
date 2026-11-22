; Extension words have no meaning until their library is imported.
!(test (str:starts-with "abc" "a") (quote (str:starts-with "abc" "a")))
!(test (json:parse "17") (quote (json:parse "17")))
!(test (__cetta_lib_str starts-with "abc" "a")
       (quote (__cetta_lib_str starts-with "abc" "a")))
!(test (import! &self ()) true)
; A descriptor naming no module raises, as a module that fails to load does,
; and imports nothing.
!(test (car-atom (catch (import! &self (str 42)))) Error)
!(test (str:starts-with "abc" "a") (quote (str:starts-with "abc" "a")))
!(test (import! &self (str json)) true)
!(test (str:starts-with "abc" "a") true)
!(test (json:parse "17") 17)
!(test (starts-with "abc" "a") (quote (starts-with "abc" "a")))
!(test (re:compile "a") (quote (re:compile "a")))
; Single descriptors and repeated imports retain their ordinary meanings.
!(test (import! &self (library str)) true)
!(test (import! &self (str str json)) true)
!(test (size-atom (collapse (match &self (= (str:starts-with $a $b) $body) t))) 1)
!(test (+ 2 3) 5)
!(test (quote (str json)) (quote (str json)))
