!(import! &self metamath)

(= (consume-until-empty $handle)
   (let $next (mm-next-stmt $handle)
     (if (== $next Empty)
         Empty
         (consume-until-empty $handle))))

!(assertEqual
  (let $handle (mm-open-file-pverify "tests/support/metamath/tiny_cverify.mm")
    (let $s1 (mm-next-stmt $handle)
      (let $s2 (mm-next-stmt $handle)
        (let $closed (mm-close-file $handle)
          ($s1 $s2 $closed)))))
  ((c ("wff" "|-" "mm.->" "(" ")"))
   (open_frame)
   ()))

!(assertEqual
  (let $handle (mm-open-file-pverify "tests/support/metamath/tiny_cverify.mm")
    (consume-until-empty $handle))
  Empty)
