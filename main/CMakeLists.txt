idf_component_register(
    SRCS "main.c"
         "demo_radio.c"
         "pupu_model.c"
         "pupu_save_codec.c"
         "pupu_clock.c"
         "pupu_sprite_math.c"
         "pupu_input.c"
         "pupu_assets.c"
         "pupu_fonts.c"
         "pupu_pet_view.c"
         "pupu_ui.c"
         "pupu_ui_egg.c"
         "pupu_ui_home.c"
         "pupu_app.c"
         "fonts/pupu_font_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
)
