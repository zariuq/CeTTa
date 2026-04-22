(= (get-type $x)
   (catch (if (== (% $x 2) 0)
              EvenNumber)))

(= (get-type (cons $head $tail))
   (if (== (get-type $head) EvenNumber)
       (if (== $tail ())
           EvenNumberList
           (get-type $tail))))

(: g (-> EvenNumberList Bool))
(= (g $L)
   True)

!(test (g (2 4 6)) True)
!(test (g (2 5)) ())
