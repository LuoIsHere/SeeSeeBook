idf_component_register(
    SRCS
        "app.cpp"
        "app_registry.cpp"
        "battery/battery_app.cpp"
        "file/file_app.cpp"
        "file/file_name.cpp"
        "gray4_test/gray4_test_app.cpp"
        "reader/reader_app.cpp"
        "menu/menu_app.cpp"
        "rtc_setting/rtc_setting_app.cpp"
        "test/test_app.cpp"
    INCLUDE_DIRS "include/app"
    PRIV_INCLUDE_DIRS "." "battery" "file" "gray4_test" "menu" "reader" "rtc_setting" "test"
    REQUIRES
        core
        services
        ui
        mooncake
)
