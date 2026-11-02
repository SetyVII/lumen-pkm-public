# Lumen Android

Native Kotlin/Jetpack Compose client with encrypted local documents, queued
offline operations and paginated server synchronization.

Requirements: JDK 17+, Android SDK platform/build tools 36 and the included
Gradle wrapper. Set `ANDROID_HOME` to your SDK installation.

From the repository root:

```sh
./mobile/gradlew -p mobile :app:testDebugUnitTest :app:assembleDebug --max-workers=2
```

The debug APK is generated under `mobile/app/build/outputs/apk/debug/`. Use a
separate emulator and disposable library for instrumentation tests. Physical
device installation and pairing require your own server and credentials.

Document 2 requires a matching `lumen-sync/2` server. The local document database
is separate from older formats. Pull data and its delivered cursor commit in a
single transaction. Remote deletion blocks, rather than discards, a pending
draft; explicit recovery creates a new note identity. Recovering annotations
does not automatically restore purged file bytes.

The interface remains under development. See dependency attributions in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
