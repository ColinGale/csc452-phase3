# Phase 3 README

**Testcase Diffs**

1. testcase 10

There are slight differences in the getTimeOfDay syscall results for some of the processes. The testcase says that these values
should be close but do not have to be exact. I believe that the values are close enough to indicate that no bugs are present withing the code.

2. testcase 20

In my output there are two adjacent lines that appear to be in the opposite order of the testcase

Expected:
```
start3(): After V -- may appear before: Child1(): After P attempt #3
Child1(): After P attempt #3 -- may appear before: start3(): After V
```

Mine:
```
Child1(): After P attempt #3 -- may appear before: start3(): After V
start3(): After V -- may appear before: Child1(): After P attempt #3
```

I do not believe this is a bug because the testcase itself says either order is allowed; both lines are printed with "may appear before" the other line. 
The two lines are not ordered by anything in the semaphore specification. start3 and Child1 are different processes, and what decides which prints first is a 
scheduling detail of how SemV wakes a waiter, which the spec does not define. The spec only requires that SemV wake a blocked process 
(or increment the value if nobody is waiting) and that waiters are not lost. I think the difference occurs because in my code I give up the lock
before waking any processes, while the testcase holds onto the lock when waking up a process.
