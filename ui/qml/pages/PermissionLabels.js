.pragma library

function label(key, supplied) {
    if (supplied)
        return supplied
    var base = key.split("/")[0]
    switch (base) {
    case "Internet": return qsTranslate("AppPermissionsPage", "Internet access")
    case "GlobalInternet": return qsTranslate("AppPermissionsPage", "Internet access to all sites")
    case "LocalNetwork": return qsTranslate("AppPermissionsPage", "Local network access")
    case "Location": return qsTranslate("AppPermissionsPage", "Location")
    case "WatchMicrophone": return qsTranslate("AppPermissionsPage", "Watch microphone")
    case "Health": return qsTranslate("AppPermissionsPage", "Health data")
    case "Notifications": return qsTranslate("AppPermissionsPage", "Read notifications")
    case "ShowNotifications": return qsTranslate("AppPermissionsPage", "Show notifications")
    case "Calendar": return qsTranslate("AppPermissionsPage", "Read calendar events")
    case "Contacts": return qsTranslate("AppPermissionsPage", "Read contacts")
    case "Timeline": return qsTranslate("AppPermissionsPage", "Write timeline pins")
    case "ReadWatchSettings": return qsTranslate("AppPermissionsPage", "Read watch settings")
    case "WriteWatchSettings": return qsTranslate("AppPermissionsPage", "Change watch settings")
    case "ReadMusic": return qsTranslate("AppPermissionsPage", "Read music playback")
    case "ControlMusic": return qsTranslate("AppPermissionsPage", "Control music playback")
    case "ReadApps": return qsTranslate("AppPermissionsPage", "List installed apps")
    case "LaunchApps": return qsTranslate("AppPermissionsPage", "Launch watch apps")
    case "FindMyPhone": return qsTranslate("AppPermissionsPage", "Find my phone")
    case "SetIndexAlarms": return qsTranslate("AppPermissionsPage", "Set Index alarms")
    case "SendMessagesBeeper": return qsTranslate("AppPermissionsPage", "Send messages through Beeper")
    case "WriteIndexNotes": return qsTranslate("AppPermissionsPage", "Write Index notes")
    case "WriteIndexReminders": return qsTranslate("AppPermissionsPage", "Write Index reminders")
    case "ReadFiles": return qsTranslate("AppPermissionsPage", "Read files")
    case "WriteFiles": return qsTranslate("AppPermissionsPage", "Write files")
    case "LegacyPebbleKit": return qsTranslate("AppPermissionsPage", "Connect to legacy companion apps")
    case "PebbleKit2": return qsTranslate("AppPermissionsPage", "Connect to companion apps")
    default: return base
    }
}

function scope(key) {
    var separator = key.indexOf("/")
    return separator < 0 ? "" : key.substring(separator + 1)
}
