(= (get-type $x)
   (catch (if (== (% $x 2) 0)
              EvenNumber)))

(: f (-> EvenNumber EvenNumber EvenNumber))
(= (f $x $y)
   (+ $x $y))

!(test (collapse (get-type 2))
       (Number EvenNumber))
!(test (f 2 4) 6)
!(test (f 3 4) ())
