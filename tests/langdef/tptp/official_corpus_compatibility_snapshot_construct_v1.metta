!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)
!(println! (tptp:snapshot-construct-compatible (system:arg 0) (system:arg 1) (system:arg 2)))
