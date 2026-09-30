plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}
android {
    namespace = "org.supermetroid.thor"
    compileSdk = 35
    ndkVersion = "27.3.13750724"
    defaultConfig {
        applicationId = "org.supermetroid.thor"
        minSdk = 33
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0-dev"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild { cmake { arguments += "-DTHOR_BUILD_TESTS=OFF" } }
    }
    externalNativeBuild {
        cmake { path = file("../../native/CMakeLists.txt"); version = "3.31.6" }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures { buildConfig = true }
    buildTypes { release { isMinifyEnabled = false } }
}
kotlin { compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) } }
