!(let $typed (the Foo a)
    (progn
      (catch (+ 1 not-a-number))
      $typed))
