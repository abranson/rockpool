pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
        maven(url = "https://jitpack.io")
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
        maven(url = "https://jitpack.io")
    }
}

rootProject.name = "libpebble3d-daemon"

// libpebble3 (the generic-Linux library) — and its whole Kotlin Multiplatform build — lives in the
// upstream mobileapp checkout, wired in here as a Gradle composite build. This is what lets the
// Sailfish daemon live in the Rockpool repo while depending on its pinned libpebble3 branch.
//
// MOBILEAPP overrides the location with a dev checkout; otherwise the pinned submodule at
// ../mobileapp (i.e. rockpool/libpebble3d/mobileapp) is used. Absolute or relative both work.
// libpebble3 declares no Maven group, so the dependency is substituted by project path, not
// coordinates — the "io.rebble.libpebblecommon:libpebble3" the daemon depends on is just a handle.
val mobileapp = System.getenv("MOBILEAPP") ?: "../mobileapp"
includeBuild(mobileapp) {
    dependencySubstitution {
        substitute(module("io.rebble.libpebblecommon:libpebble3")).using(project(":libpebble3"))
    }
}
