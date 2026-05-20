; Threaded once/select 1 must not erase an all-error frontier.

!(pragma! hyperpose-threads 2)
!(assertEqual
  (once (hyperpose ((Error hyperpose-left fail) (Error hyperpose-right fail))))
  (Error hyperpose-left fail))
!(assertEqual
  (select 1 (hyperpose ((Error hyperpose-left fail) (Error hyperpose-right fail))))
  (Error hyperpose-left fail))
