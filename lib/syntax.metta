; Source capture is separate from evaluation and scope admission. The active
; document reader owns source syntax. Every form has its own variable inventory;
; references preserve co-occurrence, not process-local variable identifiers.
; restore returns an owned opaque SourceSyntax value. write checks that the
; current reader reads the rendered text back to the same captured document.
(: syntax:read (-> %Undefined% %Undefined%))
(: syntax:restore (-> %Undefined% %Undefined%))
(: syntax:freeze (-> %Undefined% %Undefined%))
(: syntax:write (-> %Undefined% %Undefined%))
; Conservative PeTTa intrinsic head classification. False is not purity evidence.
(: syntax:intrinsic-head? (-> %Undefined% %Undefined%))
