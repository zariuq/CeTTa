; metta self-referential eval with space/type context
!(bind! &ws (new-space))
!(add-atom &ws (= (ws-fn) result))
!(assertEqual (metta (ws-fn) %Undefined% &ws) result)
