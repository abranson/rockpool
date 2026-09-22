// Run with: TZ=Europe/Paris node ui/tests/health_history_test.js
// Exercise the same ES5 helper imported by QML, including local-calendar DST handling.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const history = vm.createContext({ Date });
vm.runInContext(fs.readFileSync(path.join(__dirname, "../qml/pages/HealthHistory.js"), "utf8")
    .replace(/^\.pragma library\s*/, ""), history);

function day(date, steps, sleep, deep) {
    return { date, steps: steps === null ? 0 : steps, hasMovement: steps === null ? 0 : 1,
             sleepDuration: (sleep || 0) * 3600, deepSleepDuration: (deep || 0) * 3600,
             hasSleep: sleep === null ? 0 : 1 };
}
const source = [day("2024-03-29", 10, 6, 1), day("2024-03-30", 0, null, null),
                day("2024-03-31", null, 8, 3), day("2024-04-01", 20, 7, 2)];
const days = history.dailyRecords({ history: source });
const weeks = history.weeklyRecords(days);
assert.equal(weeks.length, 2);
assert.equal(weeks[0].date, "2024-03-25");
assert.equal(weeks[0].endDate, "2024-03-31");
assert.equal(weeks[0].steps, 10);
assert.equal(weeks[0].movementDays, 2); // Recorded zero counts; missing activity does not.
assert.equal(weeks[0].sleepDays, 2);
assert.equal(weeks[0].sleepDuration, 7 * 3600); // Missing night is excluded.
assert.equal(weeks[0].deepSleepDuration, 2 * 3600);
assert.equal(weeks[0].days, 3); // Partial first week.
assert.equal(weeks[1].date, "2024-04-01");
assert.equal(weeks[1].days, 1); // Partial current week.
assert.equal(days[0].sleepDuration, 6 * 3600); // Aggregation must not mutate daily data.

for (const date of ["2024-02-29", "2024-03-31", "2024-10-27", "2024-12-31", "2025-01-01"])
    assert.equal(history.dateString(history.localDate(date)), date);
const newYear = history.weeklyRecords(history.dailyRecords({ history:
    [day("2024-12-31", 1, null, null), day("2025-01-01", 2, null, null)] }));
assert.equal(newYear.length, 1);
assert.equal(newYear[0].date, "2024-12-30");
assert.equal(newYear[0].steps, 3);
assert.equal(newYear[0].sleepDuration, 0);

const empty = Array.from({ length: 90 }, (_, i) => {
    const date = new Date(2024, 0, 1 + i, 12);
    return day(history.dateString(date), null, null, null);
});
assert.equal(history.dailyRecords({ history: empty }).length, 7);
empty[10] = day(empty[10].date, 0, null, null);
assert.equal(history.dailyRecords({ history: empty }).length, 80);
assert.equal(history.dailyRecords({}).length, 0);
assert.equal(history.dailyRecords({ stepsWeek: [{ date: "2024-01-01", steps: 12 }],
    sleepWeek: [{ sleepDuration: 3600, deepSleepDuration: 600 }] })[0].steps, 12);
assert.equal(history.positionForDate(days, "2024-03-31", 2), 1);
assert.equal(history.positionForDate(days, "2025-01-01", 2), 2);
console.log("Health history: calendar grouping, missing data, fallback and date anchoring passed.");
