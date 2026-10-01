; A Python call that raises raises janus's error: python_error(Type, Value),
; Value the exception object, which prints as a Python object reference.
!(catch (py-call (math.sqrt -1)))
!(catch (py-call (nonexistent_module_xyz.f 1)))
!(py-call (builtins.ValueError "x"))
