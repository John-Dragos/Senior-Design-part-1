idf_component_register(
    SRCS
        "main.cc"
        "alarm_manager.cc"
        "display_manager.cc"
        "input_manager.cc"
        "storage_manager.cc"
        "time_manager.cc"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_timer
)
