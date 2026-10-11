plugins { id("com.android.application") }

android {
    signingConfigs {
        create("lab") {
            storeFile = file("motorai-lab.keystore")
            storePassword = "motorai-lab-only"
            keyAlias = "motorai-lab"
            keyPassword = "motorai-lab-only"
        }
    }

    namespace = "it.motorai.seed"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "it.motorai.seed"
        minSdk = 26
        targetSdk = 36
        versionCode = 24
        versionName = "0.21.1-seed021r1"
        externalNativeBuild {
            cmake { cppFlags += listOf("-std=c++20", "-O3") }
        }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt") }
    }

    buildTypes {
        getByName("debug") {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("lab")
        }
        getByName("release") { isMinifyEnabled = false }
    }
}
