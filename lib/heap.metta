; Persistent pairing heaps under the established finite term ordering.
; Old heaps remain usable. Equal priorities retain the kernel's LIFO order;
; use (priority serial) keys when a caller requires stable FIFO ties.
; Operations consume valid heaps made by this interface. Validate decoded
; or otherwise untrusted representations once with heap:valid?.
(: heap:empty (-> %Undefined%))
(: heap:insert (-> %Undefined% %Undefined% %Undefined% %Undefined%))
(: heap:take (-> %Undefined% %Undefined%))
(: heap:size (-> %Undefined% %Undefined%))
(: heap:valid? (-> %Undefined% %Undefined%))
