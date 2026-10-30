; Importing this library enables the shared retained-computation controls.
; work-new holds its source unevaluated. A bounded take preserves the residual;
; only explicit work-cancel or loss of the final owner releases the computation.
(: work-new (-> Atom Expression))
(: work-take (-> %Undefined% Number Expression))
(: work-budget (-> %Undefined% Number Expression))
(: work-cancel (-> %Undefined% Expression))
(: work-record (-> %Undefined% Expression))
(: work-route (-> %Undefined% Atom Expression))
(: work-count (-> %Undefined% Expression))
(: work-exists (-> %Undefined% Expression))
; The opaque epoch changes on a mutating work operation, even a zero take.
(: work:epoch (-> %Undefined% %Undefined%))
; Lifetime-qualified source read view, including live base/module dependencies.
(: work:source-epoch (-> %Undefined% %Undefined%))
(: work:source-status (-> %Undefined% %Undefined% %Undefined%))
