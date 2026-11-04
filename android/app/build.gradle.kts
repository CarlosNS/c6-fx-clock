import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Release signing comes from android/keystore.properties (not in git); without it only debug builds
val keys = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

android {
    namespace = "io.github.carlosns.c6clock"
    compileSdk = 35

    defaultConfig {
        applicationId = "io.github.carlosns.c6clock"
        minSdk = 29
        targetSdk = 35
        versionCode = 10
        versionName = "0.3.7"
    }

    signingConfigs {
        if (keys.isNotEmpty()) create("release") {
            storeFile = rootProject.file(keys.getProperty("storeFile"))
            storePassword = keys.getProperty("storePassword")
            keyAlias = keys.getProperty("keyAlias")
            keyPassword = keys.getProperty("keyPassword")
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            if (keys.isNotEmpty()) signingConfig = signingConfigs.getByName("release")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    sourceSets["main"].assets.srcDir(layout.buildDirectory.dir("generated/web"))
    testOptions.unitTests.all { it.systemProperty("repo.root", rootProject.projectDir.parentFile.absolutePath) }
}

// The interface is the clock's own web page (src/index.html + src/transport.js): one copy for both
val copyWeb by tasks.registering(Copy::class) {
    from(rootProject.file("../src")) { include("index.html", "transport.js") }
    into(layout.buildDirectory.dir("generated/web/web"))
}
tasks.named("preBuild") { dependsOn(copyWeb) }

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.activity:activity-ktx:1.9.3")
    implementation("androidx.webkit:webkit:1.12.1")
    implementation("androidx.work:work-runtime-ktx:2.9.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.json:json:20240303")  // Android's org.json is only a stub in JVM tests
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.8.1")
}
