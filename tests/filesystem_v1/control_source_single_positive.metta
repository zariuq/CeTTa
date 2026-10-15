(= (control-example:single $number)
  (let $committed (println! consumed-once)
    (if (> $number 0) (Done $number) (empty))))
!(control-example:single 3)
