(= (hide $value) (empty))

(= (mutate-on-a $value)
   (if (== $value a)
       (let $removed (remove-atom &self (item b))
            (let $added (add-atom &self (item c))
                 $value))
       $value))

!(hide ((add-atom &self (item a))
        (add-atom &self (item b))))

!(collapse (match &self (item $value)
                  (mutate-on-a $value)))

!(collapse (match &self (item $value) $value))
