idf_component_register(
    SRCS
        "paper_mono/ui_renderer.cpp"
        "paper_mono/ui_frame_pool.cpp"
        "paper_mono/views/battery_renderer.cpp"
        "paper_mono/views/file_renderer.cpp"
        "paper_mono/views/menu_renderer.cpp"
        "paper_mono/views/renderer_helpers.cpp"
        "paper_mono/views/rtc_renderer.cpp"
        "paper_mono/views/test_renderer.cpp"
        "status_bar.cpp"
        "ui_interaction_router.cpp"
        "ui_presentation.cpp"
    INCLUDE_DIRS "include/ui"
    PRIV_INCLUDE_DIRS "." "paper_mono" "paper_mono/views"
    REQUIRES
        core
        m5_hal
)
