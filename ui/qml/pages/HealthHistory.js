.pragma library

function localDate(text) {
    var parts = text.split("-")
    // Local noon avoids UTC parsing and midnight DST transitions.
    return new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]), 12)
}

function dateString(date) {
    function pad(value) { return value < 10 ? "0" + value : String(value) }
    return date.getFullYear() + "-" + pad(date.getMonth() + 1) + "-" + pad(date.getDate())
}

function dailyRecords(overview) {
    var source = overview.history
    if (!source) {
        // An older daemon still provides the original seven-day dashboard.
        source = []
        var steps = overview.stepsWeek || []
        var sleep = overview.sleepWeek || []
        for (var i = 0; i < steps.length; ++i) {
            source.push({ date: steps[i].date, steps: steps[i].steps,
                          sleepDuration: sleep[i] ? sleep[i].sleepDuration : 0,
                          deepSleepDuration: sleep[i] ? sleep[i].deepSleepDuration : 0,
                          hasMovement: 1, hasSleep: sleep[i] && sleep[i].sleepDuration > 0 ? 1 : 0 })
        }
    }
    var result = []
    for (var j = 0; j < source.length; ++j) {
        var day = source[j]
        result.push({ date: day.date, endDate: day.date, days: 1,
                      steps: Number(day.steps), sleepDuration: Number(day.sleepDuration),
                      deepSleepDuration: Number(day.deepSleepDuration),
                      movementDays: Number(day.hasMovement), sleepDays: Number(day.hasSleep) })
    }
    // Keep gaps within the history and at least the last seven dates, but do not make users
    // scroll through empty months before their first sync.
    while (result.length > 7 && !result[0].movementDays && !result[0].sleepDays)
        result.shift()
    return result
}

function weeklyRecords(days) {
    var result = []
    for (var i = 0; i < days.length; ++i) {
        var day = days[i]
        var monday = localDate(day.date)
        monday.setDate(monday.getDate() - (monday.getDay() + 6) % 7)
        var start = dateString(monday)
        var week = result.length ? result[result.length - 1] : null
        if (!week || week.date !== start) {
            week = { date: start, endDate: day.date, days: 0, steps: 0,
                     sleepDuration: 0, deepSleepDuration: 0, movementDays: 0, sleepDays: 0 }
            result.push(week)
        }
        week.endDate = day.date
        week.days++
        week.steps += day.steps
        week.sleepDuration += day.sleepDuration
        week.deepSleepDuration += day.deepSleepDuration
        week.movementDays += day.movementDays
        week.sleepDays += day.sleepDays
    }
    for (var j = 0; j < result.length; ++j) {
        var entry = result[j]
        if (entry.sleepDays) {
            entry.sleepDuration /= entry.sleepDays
            entry.deepSleepDuration /= entry.sleepDays
        }
    }
    return result
}

function maximum(entries, key) {
    var result = 1
    for (var i = 0; i < entries.length; ++i)
        result = Math.max(result, entries[i][key])
    return result
}

function positionForDate(entries, date, visibleCount) {
    for (var i = 0; i < entries.length; ++i) {
        if (entries[i].endDate >= date)
            return Math.max(0, Math.min(entries.length - visibleCount, i - visibleCount + 1))
    }
    return Math.max(0, entries.length - visibleCount)
}
