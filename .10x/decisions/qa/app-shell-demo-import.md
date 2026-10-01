# QA — app-shell-demo-import

The review with the gate table, test inventory, manual checks and open items: [`../../reviews/qa-app-shell-demo-import.md`](../../reviews/qa-app-shell-demo-import.md).

Decisions: corpus checks run locally (`GMDR_CORPUS`, `gmdr-cli verify`, `gmdr-cli gate`) because demos never enter the repository; CI covers everything that needs no demo (unit, limits, fuzzing, app build).
