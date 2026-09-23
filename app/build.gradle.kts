plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "dev.thermalview"
    compileSdk = 37
    ndkVersion = "28.1.13356709"

    defaultConfig {
        applicationId = "dev.thermalview"
        minSdk = 31      // the tablet launched on Android 12
        targetSdk = 34   // the tablet's verified API level (docs/DEVICE.md)
        versionCode = 1
        versionName = "0.1.0-m1"
        ndk { abiFilters += "arm64-v8a" }
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    externalNativeBuild {
        cmake {
            path = file("../native/android/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.core.ktx)
}
