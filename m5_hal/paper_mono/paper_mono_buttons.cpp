#include "buttons.hpp"

#include <driver/gpio.h>

#include "paper_mono_config.hpp"

bool hal_buttons_sample(button_sample& sample)
{
    sample.key1_pressed =
        gpio_get_level(static_cast<gpio_num_t>(PAPER_MONO_KEY1_GPIO)) == 0;
    sample.key2_pressed =
        gpio_get_level(static_cast<gpio_num_t>(PAPER_MONO_KEY2_GPIO)) == 0;
    return true;
}
