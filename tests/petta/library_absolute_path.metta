!(import! &self (library lib_import))
!(import_prolog_function working_dir)
!(import_prolog_function directory_file_path)
!(import_prolog_function absolute_file_name)

(= (absolute-import-fixture)
   (let* (($source-directory (working_dir))
          ($relative
            (directory_file_path
              $source-directory
              "../support/import_pkg/moduleA.metta")))
     (absolute_file_name $relative)))

!(let $absolute (absolute-import-fixture)
   (import! &self $absolute))
!(match &self (pkg-root $value) $value)
