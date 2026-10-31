!(import! &self "../../examples/kb/space_range_curriculum.metta")
(= (range-lesson-test:run Plans) (range-curriculum:plans))
(= (range-lesson-test:run (At $path)) (range-curriculum:run $path))
