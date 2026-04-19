!(import! &self lib_pln)

!(assertEqual (min 0.6 0.7) 0.6)
!(assertEqual (max 0.6 0.7) 0.7)
!(assertEqual (Truth_or 0.5 0.25) 0.625)
!(assertEqual (Truth_c2w 0.5) 1.0)
!(assertEqual (Truth_ModusPonens (stv 0.5 0.8) (stv 0.9 1.0))
              (stv 0.46 0.36000000000000004))
