!(import! &self support/pln_portable/lib_pln)

!(assertEqual (min 0.6 0.7) 0.6)
!(assertEqual (max 0.6 0.7) 0.7)
!(assertEqual (Truth_c2w 0.5) 1.0)
!(assertEqual (Truth_ModusPonens (stv 0.5 0.8) (stv 0.9 1.0)) (stv 0.46 0.8))
