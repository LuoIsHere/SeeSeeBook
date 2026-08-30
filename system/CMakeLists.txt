idf_component_register(
    SRCS
        "system_event_dispatcher.cpp"
        "system_runtime.cpp"
        "system_status_controller.cpp"
    INCLUDE_DIRS "include/system"
    REQUIRES
        app
        core
        m5_hal
        services
        ui
)
