(gslt-presentation-v1 ParserActionBytecodeCompilerV1
  (signature
    (operator compile-pack-action-program 5)
    (operator lower-pack-action 2)
    (operator lower-pack-action-dl 3)
    (operator lower-pack-action-list-dl 4)
    (operator pack-items-length 2)
    (operator pbc-apply 2)
    (operator pbc-cons 2)
    (operator pbc-push-const 1)
    (operator pbc-push-slot 1)
  )
  (equations)
  (rewrites
    (rule compile-pack-action-program-v1
      (head
        (compile-pack-action-program
          ?owner ?label ?arity ?action ?code))
      (body
        (compile-pack-production
          ?owner
          (pp-production ?label ?state ?items ?action))
        (pack-items-length ?items ?arity)
        (lower-pack-action ?action ?code)))

    (rule pack-items-length-empty
      (head (pack-items-length pp-items-nil q-zero))
      (body))
    (rule pack-items-length-cons
      (head
        (pack-items-length
          (pp-items-cons ?item ?more) (q-succ ?length)))
      (body (pack-items-length ?more ?length)))

    (rule lower-pack-action-complete
      (head (lower-pack-action ?action ?code))
      (body (lower-pack-action-dl ?action pbc-nil ?code)))
    (rule lower-pack-action-slot
      (head
        (lower-pack-action-dl
          (pa-slot ?slot)
          ?tail
          (pbc-cons (pbc-push-slot ?slot) ?tail)))
      (body))
    (rule lower-pack-action-const
      (head
        (lower-pack-action-dl
          (pa-const ?value)
          ?tail
          (pbc-cons (pbc-push-const ?value) ?tail)))
      (body))
    (rule lower-pack-action-apply
      (head
        (lower-pack-action-dl
          (pa-apply ?head ?arguments) ?tail ?code))
      (body
        (lower-pack-action-list-dl
          ?arguments
          (pbc-cons (pbc-apply ?head ?arity) ?tail)
          ?code
          ?arity)))

    (rule lower-pack-action-list-empty
      (head
        (lower-pack-action-list-dl
          pa-nil ?tail ?tail q-zero))
      (body))
    (rule lower-pack-action-list-cons
      (head
        (lower-pack-action-list-dl
          (pa-cons ?first ?more)
          ?tail
          ?code
          (q-succ ?arity)))
      (body
        (lower-pack-action-list-dl
          ?more ?tail ?more-code ?arity)
        (lower-pack-action-dl
          ?first ?more-code ?code)))
  ))
