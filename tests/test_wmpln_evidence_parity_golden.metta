!(import! &self ../lib/lib_wmpln)

; Golden parity checks for the Lean BinaryEvidence view:
; total = n+ + n-, strength = n+ / total,
; confidence = total / (total + k), and STV decoding recovers counts.

!(assertEqual (evidence-total (Evidence 5 5)) 10)
!(assertEqual (evidence-toStrength (Evidence 5 5)) 0.5)
!(assertEqual (evidence-toConfidence (Evidence 5 5) 10) 0.5)
!(assertEqual (evidence-toSTV (Evidence 5 5) 10) (stv 0.5 0.5))
!(assertEqual (evidence-ofSTV (stv 0.5 0.5) 10) (evidence 5.0 5.0))
!(assertEqual (evidence-revision (Evidence 2 3) (Evidence 4 1)) (evidence 6 4))
