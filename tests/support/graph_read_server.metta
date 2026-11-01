!(import! &self "space_query_server.metta")
!(import! &self "../../examples/native_space_graph_server.metta")
; The test combines existing handlers, not another query implementation.
(QueryPureCallbackV1 graph-read-test:count)
(= (graph-read-test:count $count $row) (+ $count 1))
(= (graph-read-test:edit (ServiceCallV1 $configuration $peer $resource $arguments))
  (let $_ (add-atom &self (= (GraphReadProgramEditedV1) changed)) ProgramEditedV1))
(= (graph-read-test:configuration)
  (let (ValueServiceV1 $socket $data $routes $limits) (query-test:configuration)
    (ValueServiceV1 $socket $data
      (list:append $routes (list:append (graph-server:routes)
        ((ServiceRouteV1 EditProgram Write graph-read-test:edit)))) $limits)))
