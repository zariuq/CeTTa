!(import! &self "space_range_curriculum.metta")
!(if (system:has-args)
   (range-curriculum:run (space-curriculum:argument (system:arg 0)))
   (CurriculumUsage "Supply an existing private store directory."))
