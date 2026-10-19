!(import! &self system)
; Each occurrence performs its own write; no cached execution or replay.
!(function
  (chain (eval (map-atom (1 1) $item
      (metta (system:write stdout "raw-callback-effect\n")
        %Undefined% &self))) $parts
    (return $parts)))
