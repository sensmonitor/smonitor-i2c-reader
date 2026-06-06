idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES smonitor-i2c
    EMBED_TXTFILES "profile.json"
)
