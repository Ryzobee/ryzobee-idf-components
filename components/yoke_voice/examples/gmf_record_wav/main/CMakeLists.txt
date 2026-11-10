idf_component_register(SRCS main.c pipeline.c runner.c INCLUDE_DIRS .
    PRIV_REQUIRES yoke_voice littlefs esp_audio_codec esp_muxer esp_timer)
littlefs_create_partition_image(recordings ../empty_recordings FLASH_IN_PROJECT)
