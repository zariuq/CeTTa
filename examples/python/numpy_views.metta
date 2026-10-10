; Requires NumPy in the Python interpreter selected by the build.
; Results are 15, 15, true. NumPy objects remain opaque foreign values.
!((py-atom int)
    (let $a ((py-atom numpy.arange) 6)
      ((py-dot $a sum))))
!((py-atom int)
    (let $a ((py-atom numpy.arange) 6)
      (let $view ((py-dot $a reshape) 2 3)
        ((py-dot $view sum)))))
!(let $a ((py-atom numpy.arange) 6)
   (let $view ((py-dot $a reshape) 2 3)
     ((py-atom numpy.shares_memory) $a $view)))
