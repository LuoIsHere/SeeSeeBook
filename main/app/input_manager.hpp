#pragma once

class app_base;

// Selects the only Mooncake app allowed to consume subsequent input events.
void input_manager_set_target(app_base* target_app);

// Drains a bounded number of HAL events and routes them to the foreground app.
void input_manager_update();
