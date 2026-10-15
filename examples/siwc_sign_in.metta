; SPDX-License-Identifier: MIT
; Keep the owned sign-in operation alive while the browser completes consent.
; CETTA_SIWC_REGISTRATION defaults to new; use a saved id for reauthorization.
; CETTA_SIWC_PORT defaults to 0 (choose an available loopback port).
; CETTA_SIWC_DEADLINE_MS defaults to 300000; zero disables the deadline.
!(import! &self siwc)
!(import! &self system)

(= (siwc-cli:await $id $shown)
   (let $event (siwc:observe $id 100)
     (case $event
       (((io:idle)
         (if $shown (siwc-cli:await $id True)
           (let* (($status (siwc:status $id))
                  ($url (llm:v1:get $status "authorization_url")))
             (if (== $url JsonNull) (siwc-cli:await $id False)
               (let $_ (println! (llm:v1:render $status))
                 (siwc-cli:await $id True))))))
        ((JsonObject $fields)
         (let* (($result (JsonObject $fields))
                ($_ (println! (llm:v1:render $result)))
                ($status (llm:v1:get $result "status")))
           (if (== $status (JsonString "ok")) True (system:exit 1))))
        ($error (let $_ (println! $error) (system:exit 1)))))))

!(let* (($registration (system:getenv-or-default "CETTA_SIWC_REGISTRATION" "new"))
        ($port-text (system:getenv-or-default "CETTA_SIWC_PORT" "0"))
        ($deadline-text (system:getenv-or-default "CETTA_SIWC_DEADLINE_MS" "300000"))
        ($port (parse $port-text)) ($deadline (parse $deadline-text))
        ($submitted (siwc:sign-in $registration $port $deadline)))
   (case $submitted
     (((io:pending $id) (siwc-cli:await $id False))
      ($error (let $_ (println! $error) (system:exit 1))))))
