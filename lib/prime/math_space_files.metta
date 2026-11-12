; Atomic filesystem checkpoints with a stable lock and optimistic revisions.
; Checkpoint data is portable; open native owners stay in the current call.
(scope:profile lexical-fresh per-call reference)
!(import! &self "./math_space_store.metta")
!(import! &self system)
!(import! &self path)

(= (math:new-store-path $requested)
   (let* (($parts (str:split "/" $requested))
          ($reverse (path:reverse-atom-expr $parts)) ($leaf (car-atom $reverse))
          ($parent-parts (path:drop-last $parts)) ($parent-text (str:join "/" $parent-parts))
          ($parent (if (== $parent-text "")
                       (if (str:starts-with $requested "/") "/" ".") $parent-text)))
     (if (or (== $leaf "") (or (== $leaf ".") (== $leaf "..")))
       (Math:StoreFailed invalid-file-path)
       (case (fs:try-canonical-path $parent)
         (((FS:CanonicalPath $directory)
            (let $target (path:join $directory $leaf) (Math:StorePath $target $directory)))
          ($other (Math:StoreFailed parent-path $other)))))))
(= (math:store-path $requested)
   (case (fs:try-canonical-path $requested)
     (((FS:CanonicalPath $target)
        (case (fs:metadata $target follow)
          (((fs:info regular $size $identity)
             (let $directory (path:dirname $target) (Math:StorePath $target $directory)))
           ($other (Math:StoreFailed not-a-regular-file $other)))))
      ($unresolved
        (case (fs:metadata $requested no-follow)
          (((fs:failed fs:missing $operation $at $errno $progress $message)
             (math:new-store-path $requested))
           ($other (Math:StoreFailed unresolved-path $other))))))))

(= (math:store-origin $project $path)
   (let $records (math:records $project)
     (new ($digest)
       (case (collapse (match $records (Math:StoreOrigin $path $digest) $digest){$path})
         ((() Math:NoOrigin)
          (($digest) (Math:Origin $digest))
          ($other (Math:StoreFailed ambiguous-origin)))))))
(= (math:set-store-origin $project $path $digest)
   (let $records (math:records $project)
     (case (math:store-origin $project $path)
       ((Math:NoOrigin
          (chain (add-atom $records (Math:StoreOrigin $path $digest)) $done Math:OriginUpdated))
        ((Math:Origin $previous)
          (chain (remove-atom $records (Math:StoreOrigin $path $previous)) $removed
            (chain (add-atom $records (Math:StoreOrigin $path $digest)) $done Math:OriginUpdated)))
        ($other $other)))))

(= (math:close-file $owner $result)
   (chain (fs:close $owner) $closed
     (case $closed
       ((fs:closed $result)
        ($other (Math:StoreUncertain close $result $other))))))
(= (math:release-lock $owner $result)
   (chain (fs:unlock $owner) $unlocked
     (case $unlocked
       ((fs:unlocked (math:close-file $owner $result))
        ($other
          (math:close-file $owner (Math:StoreUncertain unlock $result $other)))))))

(= (math:write-payload $owner $temporary $text $digest)
   (chain (fs:write-all $owner $text) $written
     (case $written
       (((fs:written $bytes)
          (if (== $bytes (str:byte-length $text))
            (chain (fs:sync $owner) $synced
              (case $synced
                ((fs:synced (Math:PendingReady $temporary $digest))
                 ($other (Math:StoreFailed sync $temporary $other)))))
            (Math:StoreFailed short-write $temporary $written)))
        ($other (Math:StoreFailed write $temporary $other))))))
(= (math:write-temporary $directory $text $digest)
   (let* (($token (system:uuid4-hex))
          ($name (str:join "" (".math-space-" $token ".pending")))
          ($temporary (path:join $directory $name)))
     (chain (fs:open-write $temporary ("create" "exclusive") 384) $opened
       (case $opened
         (((NativeHandle "file-writer" $id)
            (chain (math:write-payload $opened $temporary $text $digest) $written
              (math:close-file $opened $written)))
          ((fs:failed fs:exists $operation $at $errno $progress $message)
             (math:write-temporary $directory $text $digest))
          ($other (Math:StoreFailed temporary-open $other)))))))
(= (math:sync-directory $directory $target $digest)
   (chain (fs:open-read $directory) $opened
     (case $opened
       (((NativeHandle "file-reader" $id)
          (chain (fs:sync $opened) $synced
            (case $synced
              ((fs:synced
                 (math:close-file $opened (Math:Saved $target $digest)))
               ($other
                 (math:close-file $opened (Math:StoreUncertain directory-sync $target $digest $other)))))))
        ($other (Math:StoreUncertain directory-open $target $digest $other))))))
(= (math:publish-file $target $directory $pending)
   (case $pending
     (((Math:PendingReady $temporary $digest)
        (chain (fs:rename $temporary $target) $renamed
          (case $renamed
            ((() (math:sync-directory $directory $target $digest))
             ($other (Math:StoreFailed rename $temporary $target $other))))))
      ($other $other))))

(= (math:store-current $target)
   (case (fs:read-text-result $target 0 raw)
     ((("ok" $text)
        (let $digest (str:sha256 $text) (Math:Current $digest $text)))
      (("missing" $directory) Math:StoreMissing)
      ($other (Math:StoreFailed read $other)))))
(= (math:can-save $origin $current)
   (case ($origin $current)
     (((Math:NoOrigin Math:StoreMissing) True)
      (((Math:Origin $expected) (Math:Current $actual $text)) (== $expected $actual))
      ($other False))))
(= (math:store-revision $current)
   (case $current
     (((Math:Current $digest $text) (Math:Revision $digest))
      ($other $other))))
(= (math:save-locked $project $target $directory $text $digest)
   (let* (($origin (math:store-origin $project $target)) ($current (math:store-current $target)))
     (if (math:can-save $origin $current)
       (chain (math:write-temporary $directory $text $digest) $pending
         (math:publish-file $target $directory $pending))
       (let $revision (math:store-revision $current)
         (Math:StoreConflict $target $origin $revision)))))
(= (math:save-with-owner $project $target $directory $text $digest $owner)
   (chain (fs:lock $owner exclusive) $locked
     (case $locked
       ((fs:locked
          (chain (math:save-locked $project $target $directory $text $digest) $result
            (math:release-lock $owner $result)))
        ($other (math:close-file $owner (Math:StoreFailed lock $other)))))))
(= (math:save-prepared $project $target $directory $text)
   (let* (($lock-path (str:concat $target ".lock")) ($digest (str:sha256 $text)))
     (chain (fs:open-write $lock-path ("create") 384) $opened
       (case $opened
         (((NativeHandle "file-writer" $id)
            (chain (math:save-with-owner $project $target $directory $text $digest $opened) $result
              (case $result
                (((Math:Saved $saved $revision)
                   (chain (math:set-store-origin $project $target $revision) $updated $result))
                 ($other $other)))))
          ($other (Math:StoreFailed lock-open $other)))))))
(= (math:save-validated $project $target $directory $text $budget)
   (case (math:restore $text $budget)
     (((Math:Restored $checked) (math:save-prepared $project $target $directory $text))
      ($other (Math:StoreFailed checkpoint-validation $other)))))
(= (math:save-at $project $target $directory $budget)
   (let $text (math:serialize $project)
     (if (== (std:get-type $text) String)
       (math:save-validated $project $target $directory $text $budget)
       $text)))
(= (math:save $project $requested $budget)
   (case (math:store-path $requested)
     (((Math:StorePath $target $directory) (math:save-at $project $target $directory $budget))
      ($other $other))))

; Reading and proof replay are separate. The immutable bytes are captured
; under the file lock; native reconstruction runs after releasing that lock.
(= (math:load-with-owner $target $owner)
   (chain (fs:lock $owner shared) $locked
     (case $locked
       ((fs:locked
          (chain (math:store-current $target) $current
            (math:release-lock $owner $current)))
        ($other (math:close-file $owner (Math:StoreFailed lock $other)))))))
(= (math:load-captured $target $current $budget)
   (case $current
     (((Math:Current $digest $text)
        (case (math:restore $text $budget)
          (((Math:Restored $project)
             (chain (math:set-store-origin $project $target $digest) $updated
               (Math:Loaded $project $digest)))
           ($other $other))))
      ($other $other))))
(= (math:load-at $target $budget)
   (let $lock-path (str:concat $target ".lock")
     (chain (fs:open-write $lock-path ("create") 384) $opened
       (case $opened
         (((NativeHandle "file-writer" $id)
            (chain (math:load-with-owner $target $opened) $current
              (math:load-captured $target $current $budget)))
          ($other (Math:StoreFailed lock-open $other)))))))
(= (math:load $requested $budget)
   (case (math:store-path $requested)
     (((Math:StorePath $target $directory) (math:load-at $target $budget))
      ($other $other))))
