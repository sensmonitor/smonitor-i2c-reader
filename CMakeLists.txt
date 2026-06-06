idf_component_register(
    SRCS
        "src/smonitor_i2c.cpp"
        "src/smonitor_i2c_native.cpp"
        "src/smonitor_i2c_bosch.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES i2c_bus json freertos
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
