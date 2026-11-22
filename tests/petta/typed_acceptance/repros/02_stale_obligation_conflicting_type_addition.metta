!(let $typed (the Foo a)
    (let (add-atom &self (: a Bar)) $_
      $typed))
