idf_component_register(
    SRCS
        "src/smonitor_i2c.cpp"
        "src/smonitor_i2c_native.cpp"
        "src/smonitor_i2c_bosch.cpp"
        "src/smonitor_i2c_profiles.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    EMBED_TXTFILES
        "profiles/ads1115.json"
        "profiles/aht20.json"
        "profiles/bh1750.json"
        "profiles/bme280.json"
        "profiles/bmp280.json"
        "profiles/ina219.json"
        "profiles/mpu6050.json"
        "profiles/qmc5883l.json"
        "profiles/sht31.json"
        "profiles/vl53l0x.json"
    REQUIRES i2c_bus json freertos
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
