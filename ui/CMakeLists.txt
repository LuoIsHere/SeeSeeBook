idf_component_register(
    SRCS
        "paper_mono/ui_renderer.cpp"
        "status_bar.cpp"
        "ui_interaction_router.cpp"
    INCLUDE_DIRS "include/ui"
    PRIV_INCLUDE_DIRS "." "paper_mono"
    REQUIRES
        core
        m5_hal
)
