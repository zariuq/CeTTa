(= (hide $result) (empty))

!(hide ((add-atom &self (friend tim tom))
        (add-atom &self (friend tom tam))
        (add-atom &self (friend sim som))
        (add-atom &self (friend som sam))))

!(hide (match &self (friend $a $b)
         (match &self (friend $b $c)
           ((add-atom &self (transitive $a $b $c))
            (remove-atom &self (friend $a $b))
            (remove-atom &self (friend $b $c))))))

!(msort (collapse (match &self (transitive $a $b $c)
                              (transitive $a $b $c))))
!(collapse (match &self (friend $a $b) (friend $a $b)))
