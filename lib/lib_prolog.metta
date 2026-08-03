(: prolog:available? (-> Bool))

(= (prolog:available?)
   (function
     (case (__cetta_lib_prolog_available ())
       (($result (return $result))))))

(= (prolog:query $goal $projection)
   (function
     (case (__cetta_lib_prolog_query $goal $projection)
       (($result (return $result))))))
