!(import! &self system)
(= (value-plus $x) (+ $x 1))
!(let $before (map-atom (1) $item (value-plus $item))
  (let $changed (add-atom &self
      (= (value-plus $x)
        (let $effect (system:write stdout "revision-effect\n") (+ $x 2))))
    (let $after (map-atom (1) $item (value-plus $item))
      (PreparationChanged $before $after))))
