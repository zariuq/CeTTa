; Explicit ATP library umbrella.  Importing this module enables proof-search
; producers without adding their policy vocabulary to the Prime core surface.
!(import! &self ./atp/direct)
!(import! &self ./atp/indexed)
!(import! &self ./atp/resolution)
!(import! &self ./atp/resolution_search)
!(import! &self ./atp/kbo)
!(import! &self ./atp/superposition)
!(import! &self ./atp/agenda)
