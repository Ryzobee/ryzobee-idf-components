function(gmf_ci1302_codec_workaround)
idf_build_get_property(ci1302_build_components BUILD_COMPONENTS)
if(NOT "espressif__esp_codec_dev" IN_LIST ci1302_build_components)
    return()
endif()
# esp_codec_dev 2.0.0-beta5 在没有内置 codec 时生成空注册表，
# 查找循环的 size_t < 0 会触发 -Wtype-limits；仅对该配置和源文件豁免。
# 保留 managed_components 原始源码，启用任一内置 codec 后恢复该诊断。
if(NOT (CONFIG_CODEC_ES8311_SUPPORT OR CONFIG_CODEC_ES7210_SUPPORT
        OR CONFIG_CODEC_ES7243_SUPPORT OR CONFIG_CODEC_ES7243E_SUPPORT
        OR CONFIG_CODEC_ES8156_SUPPORT OR CONFIG_CODEC_ES8374_SUPPORT
        OR CONFIG_CODEC_ES8388_SUPPORT OR CONFIG_CODEC_ES8389_SUPPORT
        OR CONFIG_CODEC_AW88298_SUPPORT OR CONFIG_CODEC_TAS5805M_SUPPORT
        OR CONFIG_CODEC_ZL38063_SUPPORT OR CONFIG_CODEC_CJC8910_SUPPORT
        OR CONFIG_CODEC_DUMMY_SUPPORT))
    idf_component_get_property(codec_dev_dir espressif__esp_codec_dev COMPONENT_DIR)
    set_property(SOURCE "${codec_dev_dir}/src/audio_codec_factory.c"
                 DIRECTORY "${codec_dev_dir}"
                 APPEND PROPERTY COMPILE_OPTIONS "-Wno-type-limits")
endif()

endfunction()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL gmf_ci1302_codec_workaround)
