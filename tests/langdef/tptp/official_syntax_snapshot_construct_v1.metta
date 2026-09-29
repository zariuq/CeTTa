!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)
!(println! (tptp:snapshot-construct (system:arg 0) (system:arg 1)))
