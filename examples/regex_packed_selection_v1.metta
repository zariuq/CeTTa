!(import! &self langdef)

; A selection observes existing families; it does not change the grammar.
(= (selection-example:summary $ordinary $selected)
   (case ($ordinary $selected)
     ((((ParserWithForestV1 (ParserAcceptedV1 $all $span) $forest)
        (ParserWithForestV1 (ParserAcceptedV1 $kept $selected-span) $selected-forest))
       (let* (($all-count (size-atom $all))
              ($kept-count (size-atom $kept))
              ($preserved (if (== $forest $selected-forest) ForestPreservedV1 ForestChangedV1)))
         (function (return (RegexPackedSelectionV1 $all-count $kept-count $preserved)))))
      ($other (function (return (RegexPackedSelectionUnexpectedV1 $other)))))))

; Successful resolved views return the same selected values without an
; encoded forest. The owned answer remains valid after the reader is closed.
(= (selection-example:value-summary $observation)
   (case $observation
     (((ParserAcceptedV1 $values $span)
       (let $count (size-atom $values)
         (function (return (RegexSelectedViewV1 $count)))))
      ($other (function (return (RegexPackedSelectionUnexpectedV1 $other)))))))

!(let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
        ($view (langdef:parser-parse-text-selected $reader "\\12\\34"
          (ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 "re-src:backref" "RxDigitClass") LNil))))
        ($closed (langdef:close-parser $reader)))
   (selection-example:value-summary $view))

!(let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
        ($view (langdef:parser-parse-text-selected $reader "\\١2\\３4"
          (ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 "re-src:backref" "RxDigitClass") LNil))))
        ($closed (langdef:close-parser $reader)))
   (selection-example:value-summary $view))

; A value-only request does not discard unresolved selection evidence.
!(let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
        ($view (langdef:parser-parse-text-selected $reader "\\12"
          (ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 "re-src:backref" "AbsentClass") LNil))))
        ($closed (langdef:close-parser $reader)))
   (case $view
     (((ParserWithForestV1 (ParserSelectionObligationV1 $request $reason) $forest)
       (function (return RegexSelectionUnresolvedV1)))
      ($other (function (return (RegexPackedSelectionUnexpectedV1 $other)))))))

; Both numeric references consume their maximal digit run, before products.
!(let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
        ($ordinary (langdef:parser-parse-text-forest $reader "\\12\\34" 0))
        ($selected (langdef:parser-parse-text-forest-selected $reader "\\12\\34"
          (ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 "re-src:backref" "RxDigitClass") LNil)) 0))
        ($closed (langdef:close-parser $reader)))
   (selection-example:summary $ordinary $selected))

; Decimal digits from different scripts obey the same declared class.
!(let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
        ($ordinary (langdef:parser-parse-text-forest $reader "\\١2\\３4" 0))
        ($selected (langdef:parser-parse-text-forest-selected $reader "\\١2\\３4"
          (ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 "re-src:backref" "RxDigitClass") LNil)) 0))
        ($closed (langdef:close-parser $reader)))
   (selection-example:summary $ordinary $selected))

; An empty selection preserves ambiguity rather than choosing the first tree.
!(let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
        ($ordinary (langdef:parser-parse-text-forest $reader "\\12" 0))
        ($selected (langdef:parser-parse-text-forest-selected $reader "\\12" (ParserSelectionV1 LNil) 0))
        ($closed (langdef:close-parser $reader)))
   (selection-example:summary $ordinary $selected))

; A missing class leaves an interpretation obligation with the original forest.
!(let* (($reader (langdef:load-baked-parser "regex-pattern-reader"))
        ($selected (langdef:parser-parse-text-forest-selected $reader "\\12"
          (ParserSelectionV1 (LCons (ParserRejectFollowingClassV1 "re-src:backref" "AbsentClass") LNil)) 0))
        ($closed (langdef:close-parser $reader)))
   (case $selected
     (((ParserWithForestV1 (ParserSelectionObligationV1 $request $reason) $forest)
       (function (return RegexSelectionUnresolvedV1)))
      ($other (function (return (RegexPackedSelectionUnexpectedV1 $other)))))))
