#pragma once

struct button_sample {
    bool key1_pressed;
    bool key2_pressed;
};

// Reads the built-in PaperMono buttons. Both inputs are active-low.
bool hal_buttons_sample(button_sample& sample);
