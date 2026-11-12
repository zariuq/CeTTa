!(import! &self (library llm))
(= (stream-probe:call (Decode $protocol $stream $body $limit))
   (llm:v1:response-body $protocol $stream $body $limit))
