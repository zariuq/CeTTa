!(import! &self "../../lib/lib_module.metta")
!(import! &self system)
!(import! &self str)
!(let*
  (($directory (system:entry-directory))
   ($path (str:concat $directory "/FilesystemProtocol.module"))
   ($reader (langdef:load-baked-parser "module-core-reader"))
   ($source (module:read-file $reader "filesystem-protocol" $path))
   ($closed-reader (langdef:close-parser $reader))
   ($presentation (module:assemble $source "Example" LNil LNil OriginViewV1 "filesystem:unfinished"))
   ($core (module:core $presentation "FilesystemProtocol"))
   ($authority (langdef:load-language-value $core))
   ($paused (langdef:rewrite-term-at-data $authority "Scan" "Scan" 4 100000
               (LangDef:RelationRows) (FsObserve (FsPaused))))
   ($fault (langdef:rewrite-term-at-data $authority "Scan" "Scan" 4 100000
               (LangDef:RelationRows) (FsObserve (FsFault (PermissionFault)))))
   ($cancelled (langdef:rewrite-term-at-data $authority "Scan" "Scan" 4 100000
               (LangDef:RelationRows) (FsObserve (FsInterrupted (Cancelled)))))
   ($wrong-kind (langdef:admit-term-data $authority "Scan" (FsObserve (FsFault (EmptyBytes)))))
   ($closed-authority (langdef:close-language $authority)))
  (function (return (FilesystemProtocolUnfinished $paused $fault $cancelled $wrong-kind))))
