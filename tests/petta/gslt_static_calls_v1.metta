(gslt-presentation-v1 StaticCallsV1
  (signature
    (operator CopyListV1 2)
    (operator CopyUseV1 1)
    (operator ListConsV1 2)
    (operator OrderedChoiceV1 1)
    (operator ChoiceUseV1 1))
  (equations)
  (rewrites
    (rule copy-empty-v1
      (head (CopyListV1 ListNilV1 ListNilV1))
      (body))
    (rule copy-cons-v1
      (head (CopyListV1 (ListConsV1 ?head ?tail)
                        (ListConsV1 ?head ?copied)))
      (body (CopyListV1 ?tail ?copied)))
    (rule copy-use-v1
      (head (CopyUseV1 ?copied))
      (body (CopyListV1 (ListConsV1 FirstV1
                         (ListConsV1 SecondV1 ListNilV1)) ?copied)))
    (rule choice-first-v1
      (head (OrderedChoiceV1 FirstV1))
      (body))
    (rule choice-second-v1
      (head (OrderedChoiceV1 SecondV1))
      (body))
    (rule choice-duplicate-v1
      (head (OrderedChoiceV1 FirstV1))
      (body))
    (rule choice-use-v1
      (head (ChoiceUseV1 ?result))
      (body (OrderedChoiceV1 ?result)))))
