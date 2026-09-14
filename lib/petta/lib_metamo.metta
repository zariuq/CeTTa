; Tiny "metamo" MVP for PeTTa.
;
; Idea:
;   - represent goals in one common shape: (Goal task priority context)
;   - treat "metamo" as operators over goals, not as a separate agent
;   - domain files define:
;       (primitive-task <task>) -> True/False
;       (decompose-task-in-context <task> <context>) -> tuple of subtasks or ()
;   - the library preserves goal metadata while rewriting high-level goals into
;     subgoals and then into a primitive plan.

(: Metamo__task (-> Atom Atom))
(= (Metamo__task (Goal $task $priority $context))
   $task)

(: Metamo__priority (-> Atom Atom))
(= (Metamo__priority (Goal $task $priority $context))
   $priority)

(: Metamo__context (-> Atom Atom))
(= (Metamo__context (Goal $task $priority $context))
   $context)

(: Metamo__with-task (-> Atom Atom Atom))
(= (Metamo__with-task (Goal $oldtask $priority $context) $newtask)
   (Goal $newtask $priority $context))

(: Metamo__primitive-goal (-> Atom Bool))
(= (Metamo__primitive-goal (Goal $task $priority $context))
   (primitive-task $task))

(: Metamo__expand-once (-> Atom Atom))
(= (Metamo__expand-once $goal)
   (map-atom (decompose-task-in-context (Metamo__task $goal) (Metamo__context $goal))
             $subtask
             (Metamo__with-task $goal $subtask)))

; "No plan found" is two different situations, and the context tells us which:
;   - if the context contains the marker  thats-all  it is CLOSED: we declared
;     "these facts are all the facts", so no decomposition means the task is
;     genuinely impossible here -> (ImpossibleGoal ...)
;   - otherwise the context is OPEN: we only failed to find a plan *within
;     what we know*; means may exist beyond the listed facts -> (OpenGoal ...)
; Collapsing "no plan found" into "impossible" is a real decision an agent
; makes (adding thats-all), never a silent default.
(: Metamo__blocked-leaf (-> Atom Atom))
(= (Metamo__blocked-leaf (Goal $task $priority $context))
   (if (is-member thats-all $context)
       ((ImpossibleGoal $task $priority $context))
       ((OpenGoal $task $priority $context))))

; Abductive reframing: propose ALTERNATIVE tasks for a goal we could not plan.
; Domain files supply:
;   (reframe-task <task> <context>) -> tuple of alternative tasks
; Two teaching points baked in:
;   - reframing returns SEVERAL candidates (abduction has spread; never force
;     a unique fallback)
;   - only an OpenGoal can be reframed by search; an ImpossibleGoal needs its
;     thats-all assumption revised first, not more search
(: Metamo__reframe (-> Atom Atom))
(= (Metamo__reframe (OpenGoal $task $priority $context))
   (map-atom (reframe-task $task $context)
             $alt
             (Goal $alt $priority $context)))
(= (Metamo__reframe (ImpossibleGoal $task $priority $context))
   ())

; Possible next operators, same spirit (one line each to jog ideas):
;   Metamo__reopen  : (ImpossibleGoal ...) -> (OpenGoal ...) by explicitly
;                     retracting thats-all -- assumption revision as an operator
;   Metamo__probe   : for an OpenGoal, emit an information-gathering subgoal
;                     (e.g. check_for microwave) -- curiosity targets open goals
;   interval priorities: (lo hi) instead of one number; select by dominance,
;                     and ties go to the widest interval (explore!)
;   Metamo__refine  : lazy decomposition -- plan a finite prefix now, deepen
;                     granularity on demand (goals need not bottom out)

(: Metamo__plan-goals (-> Atom Atom))
(= (Metamo__plan-goals ())
   ())

(= (Metamo__plan-goals (cons $goal $rest))
   (append (Metamo__plan $goal)
           (Metamo__plan-goals $rest)))

(: Metamo__plan (-> Atom Atom))
(= (Metamo__plan $goal)
   (if (Metamo__primitive-goal $goal)
       ($goal)
       (let $children (Metamo__expand-once $goal)
            (if (== $children ())
                (Metamo__blocked-leaf $goal)
                (Metamo__plan-goals $children)))))
