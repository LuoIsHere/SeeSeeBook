idf_component_register(
    SRCS
        "app.cpp"
        "app_registry.cpp"
        "battery/battery_app.cpp"
        "file/file_app.cpp"
        "file/file_name.cpp"
        "reader/reader_app.cpp"
        "menu/menu_app.cpp"
        "rtc_setting/rtc_setting_app.cpp"
        "test/test_app.cpp"
    INCLUDE_DIRS "include/app"
    PRIV_INCLUDE_DIRS "." "battery" "file" "menu" "reader" "rtc_setting" "test"
    REQUIRES
        core
        services
        ui
        mooncake
)
