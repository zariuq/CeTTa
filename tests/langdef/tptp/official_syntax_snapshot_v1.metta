!(import! &self ../../../lib/lib_tptp.metta)
!(import! &self system)
!(println! (tptp:snapshot-info (system:arg 0)))
