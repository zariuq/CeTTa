; Captured module space values remain usable for ordinary queries and writes,
; but import! accepts only the currently active module's &self.
(= (import-from-module) (import! &self mi_moda))
