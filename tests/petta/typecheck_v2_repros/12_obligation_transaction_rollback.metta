!(let $typed (the Foo a)
    (let $_
      (superpose
        ((transaction
           (progn
             (add-atom &self (: b Bar))
             (empty)))
         rollback-recovered))
      $typed))
