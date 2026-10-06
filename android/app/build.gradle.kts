plugins { id("com.android.application") }

android {
    namespace = "it.motorai.seed"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "it.motorai.seed"
        minSdk = 26
        targetSdk = 36
        versionCode = 3
        versionName = "0.3.0-seed003"
        externalNativeBuild {
            cmake { cppFlags += listOf("-std=c++20", "-O3") }
        }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    buildTypes {
        getByName("debug") { isMinifyEnabled = false }
        getByName("release") { isMinifyEnabled = false }
    }
}
