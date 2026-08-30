idf_component_register(
    SRCS
        "paper_mono/paper_mono.cpp"
        "paper_mono/paper_mono_battery.cpp"
        "paper_mono/paper_mono_display.cpp"
        "paper_mono/paper_mono_internal_i2c.cpp"
        "paper_mono/paper_mono_rtc.cpp"
        "paper_mono/paper_mono_storage.cpp"
        "paper_mono/paper_mono_touch.cpp"
    INCLUDE_DIRS "include/hal"
    REQUIRES
        core
        m5unified
        esp_driver_sdmmc
        fatfs
)
