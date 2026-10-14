!(import! &self system)
!(let $first (system:write stdout "first\n")
  (let $second (system:write stdout "second\n") (SequenceOrdered)))
