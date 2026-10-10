; Make call_helpers.py importable, for example with PYTHONPATH=examples/python.
; Results are 6.0 and true.
!(let $a ((py-atom call_helpers.call_with_keywords)
           (py-atom numpy.ones) ((2 3)) ((dtype (py-atom numpy.float64))))
   ((py-atom float) ((py-dot $a sum))))
!(let $a ((py-atom call_helpers.call_with_keywords)
           (py-atom numpy.ones) ((2 3)) ((dtype (py-atom numpy.float64))))
   ((py-atom operator.eq) (py-dot $a dtype) (py-atom numpy.float64)))
