; Modules read from files, with the modules they import.
;
; (module-files:read-with-imports READER PATH) reads the module at PATH and,
; beside it, every module it imports and theirs: an import "X.module" of a
; module at DIR/M.module is DIR/X.module.  The answer is
; (ModuleFilesV1 MODULE CATALOG), the catalog in the form module:assemble
; resolves imports in.

!(import! &self (library lib_module))
!(import! &self str)

(= (module-files:but-last $xs)
   (let ($head $tail) (decons-atom $xs)
     (if (== $tail ()) () (cons-atom $head (module-files:but-last $tail)))))

(= (module-files:dir-of $path)
   (let $dir (module-files:but-last (str:split "/" $path))
     (if (== $dir ()) "" (str:concat (str:join "/" $dir) "/"))))

(= (module-files:append LNil $ys) $ys)
(= (module-files:append (LCons $x $xs) $ys) (LCons $x (module-files:append $xs $ys)))

(= (module-files:imports (ModuleValueV1 $ns $name $imports $theories $entries $others $sites)) $imports)

(= (module-files:location (ModuleImportAsV1 $location $alias)) $location)
(= (module-files:location (ModuleImportFromV1 $alias $location)) $location)

(= (module-files:catalog $reader $dir LNil) LNil)
(= (module-files:catalog $reader $dir (LCons $import $rest))
   (let* (($location (module-files:location $import))
          ($path (str:concat $dir $location))
          ($m (module:read-file $reader $location $path))
          ($theirs (module-files:catalog $reader (module-files:dir-of $path) (module-files:imports $m))))
     (LCons (ModuleCatalogEntryV1 $location $m)
            (module-files:append $theirs (module-files:catalog $reader $dir $rest)))))

(= (module-files:read-with-imports $reader $path)
   (let* (($module (module:read-file $reader "module" $path))
          ($catalog (module-files:catalog $reader (module-files:dir-of $path) (module-files:imports $module))))
     (ModuleFilesV1 $module $catalog)))
