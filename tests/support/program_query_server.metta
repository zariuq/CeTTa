!(import! &self "space_query_server.metta")
(= (program-server:edit (ServiceCallV1 $configuration $peer $resource $arguments))
  (let $_ (add-atom &self (= (ServerProgramChangedV1) changed)) ProgramEditedV1))
(= (program-server:configuration)
  (let (ValueServiceV1 $socket $data $routes $limits) (query-test:configuration)
    (ValueServiceV1 $socket $data
      (list:append $routes ((ServiceRouteV1 EditProgram Write program-server:edit))) $limits)))
