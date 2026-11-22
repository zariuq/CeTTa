!(let $typed (the Foo a)
    (let (add-atom &self (: b Bar)) $_
      $typed))
