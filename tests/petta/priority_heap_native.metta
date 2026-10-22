!(import_prolog_function (superpose (empty_heap add_to_heap get_from_heap heap_size)))

!(println!
  (let $h0 (empty_heap)
    (let $h1 (add_to_heap $h0 3 low)
      (let $h2 (add_to_heap $h1 1 first)
        (let $h3 (add_to_heap $h2 1 second)
          (let $h4 (add_to_heap $h3 4 last)
            (let $h5 (get_from_heap $h4 $k1 $v1)
              (let $h6 (get_from_heap $h5 $k2 $v2)
                (let $h7 (get_from_heap $h6 $k3 $v3)
                  (let $h8 (get_from_heap $h7 $k4 $v4)
                    (PriorityHeapNative
                      $h4 (heap_size $h0) (heap_size $h4)
                      $k1 $v1 $k2 $v2 $k3 $v3 $k4 $v4
                      (heap_size $h8)
                      (collapse (get_from_heap $h8 $empty-key $empty-value)))))))))))))
