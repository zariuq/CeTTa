!(import! &self (library lib_petta_probe))
!(assertEqual (petta-library-probe) ok)

!(import! &self (library str))
!(assertEqual (str-length "hello") 5)
