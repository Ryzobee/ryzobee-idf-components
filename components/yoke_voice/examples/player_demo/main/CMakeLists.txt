idf_component_register(SRCS main.c PRIV_REQUIRES yoke_voice littlefs)
littlefs_create_partition_image(res ../resources FLASH_IN_PROJECT)
littlefs_create_partition_image(recordings ../empty_recordings FLASH_IN_PROJECT)
