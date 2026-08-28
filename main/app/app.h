#pragma once

// Initializes the application state and requests the first full-quality frame.
void app_init();

// Processes one touch event and submits a new frame when required.
void app_loop();
