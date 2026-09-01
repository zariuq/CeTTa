!(import! &self (library lib_import))
!(import_prolog_function working_dir)
!(import_prolog_function directory_file_path)
!(import_prolog_function absolute_file_name)

(= (libraryPathFixture $relative)
   (let* (($sourceDir (working_dir))
          ($joined (directory_file_path $sourceDir $relative)))
     (absolute_file_name $joined)))

!(let* (($first (libraryPathFixture "../support/library_path_order/first/pkg"))
         ($second (libraryPathFixture "../support/library_path_order/second/pkg"))
         ($suffixMiss (libraryPathFixture "../support/library_path_order/third/pkg-extra"))
         ($missingFirst (libraryPathFixture "../support/library_path_order/missing/pkg")))
   (progn
     (assertzPredicate (Predicate (library_path $first)))
     (assertaPredicate (Predicate (library_path $second)))
     (test (size-atom (collapse (library pkg choice))) 2)
     (test (== (once (library pkg choice))
               (directory_file_path $second choice)) True)

     (retractPredicate (Predicate (library_path $second)))
     (test (size-atom (collapse (library pkg choice))) 1)
     (test (== (once (library pkg choice))
               (directory_file_path $first choice)) True)

     (assertzPredicate (Predicate (library_path $second)))
     (test (== (once (library pkg choice))
               (directory_file_path $first choice)) True)
     (assertzPredicate (Predicate (library_path $first)))
     (test (size-atom (collapse (library pkg choice))) 3)

     (retractPredicate (Predicate (library_path $first)))
     (test (size-atom (collapse (library pkg choice))) 2)
     (test (== (once (library pkg choice))
               (directory_file_path $second choice)) True)

     ; Suffix selection is exact, and unrelated foreign predicates do not
     ; acquire library-path authority merely because their payload is a path.
     (assertzPredicate (Predicate (library_path $suffixMiss)))
     (test (size-atom (collapse (library pkg choice))) 2)
     (assertzPredicate (Predicate (not_library_path $first)))
     (test (size-atom (collapse (library pkg choice))) 2)

     ; A failed first-occurrence retraction is a negative observation and
     ; leaves the ordered relation unchanged.
     (test (retractPredicate
             (Predicate (library_path path-that-was-never-authored))) false)
     (test (size-atom (collapse (library pkg choice))) 2)

     ; A missing first candidate is the empty bag, not an abort: later roots
     ; remain eligible.  Both later successful occurrences are imported.
     (assertaPredicate (Predicate (library_path $missingFirst)))
     (test (size-atom (collapse (library pkg choice))) 3)))

!(import! &self (library pkg choice))
!(test (once (root-choice)) second)
!(test (collapse (root-choice)) (second first))
