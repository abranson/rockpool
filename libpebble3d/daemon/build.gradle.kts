plugins {
    kotlin("jvm") version "2.3.10"
    application
    // NB: no Compose Gradle/compiler plugins. The daemon has no @Composables — it only needs the
    // compose ui-graphics *library* (ImageBitmap + asSkiaBitmap, for screenshot encoding). Applying
    // the Compose compiler plugin here was actively harmful: it stamps @StabilityInferred + a $stable
    // field onto the org.rockpool D-Bus signal classes (e.g. RockpoolPebble$ConnectionStateChanged),
    // which corrupts their InnerClasses metadata so native-image can't register them and dbus-java
    // then can't dispatch the neighbouring methods (ConnectionState() came back UnknownMethod).
}

dependencies {
    // The generic-Linux libpebble3 library, from the mobileapp composite build (settings.gradle.kts).
    // Gradle resolves its jvm variant automatically.
    implementation("io.rebble.libpebblecommon:libpebble3") {
        // The JDK-native UNIX socket transport used by libpebble3 cannot pass file descriptors.
        // Rockpool's primary install API uses D-Bus `h`, so replace only the daemon's transitive
        // transport while leaving the reusable libpebble3 JVM dependency unchanged.
        exclude(
            group = "com.github.hypfvieh",
            module = "dbus-java-transport-native-unixsocket",
        )
    }

    // The primary API and temporary compatibility interfaces expose dbus-java types.
    implementation("com.github.hypfvieh:bluez-dbus:0.3.5")
    implementation("com.github.hypfvieh:dbus-java-transport-junixsocket:5.2.0")
    implementation("co.touchlab:kermit:2.0.8")
    implementation("io.insert-koin:koin-core:4.1.1")
    // compose ui-graphics as a plain library (desktop/JVM variant) for ImageBitmap + asSkiaBitmap;
    // pulls skiko transitively. No Compose compiler plugin — see the plugins block.
    implementation("org.jetbrains.compose.ui:ui-graphics-desktop:1.10.1")
    testImplementation(kotlin("test"))
}

kotlin {
    jvmToolchain(17)
    compilerOptions {
        // Match libpebble3's opt-ins for the experimental APIs the daemon code uses.
        optIn.addAll(
            "kotlin.uuid.ExperimentalUuidApi",
            "kotlin.ExperimentalUnsignedTypes",
            "kotlin.ExperimentalStdlibApi",
            "kotlin.time.ExperimentalTime",
            "kotlinx.coroutines.FlowPreview",
            "kotlinx.coroutines.ExperimentalCoroutinesApi",
        )
    }
}

application {
    mainClass.set("io.rebble.libpebblecommon.Daemon")
}

// Collects the daemon jar + its full runtime classpath into build/jvmDist/libs, the flat dir the
// GraalVM native-image container build consumes (see ../build.sh). A plain-JVM module's
// runtimeClasspath is transitive, so this pulls in libpebble3 and all its runtime deps too.
//
// Sync (not Copy) so the dir mirrors the current classpath: a Copy leaves stale jars behind, and a
// lingering dbus-java-transport-native-unixsocket.jar from before the junixsocket swap put two UNIX
// transports in the image, which dbus-java refuses to start with (TransportRegistrationException).
val jvmDist by tasks.registering(Sync::class) {
    group = "distribution"
    description = "Collects the runtime classpath for the headless Sailfish daemon"
    from(tasks.named("jar"))
    from(configurations.getByName("runtimeClasspath"))
    into(layout.buildDirectory.dir("jvmDist/libs"))
}
