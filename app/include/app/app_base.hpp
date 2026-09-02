#pragma once

#include <mooncake.h>

#include "app_event.hpp"
#include "app_launch_context.hpp"

class app_base : public mooncake::AppAbility {
public:
    virtual void handle_app_event(const app_event& event) = 0;
    // Called synchronously before Mooncake opens the target. A target must copy
    // anything it needs; the runtime clears its launch context after this call.
    virtual bool prepare_launch(const app_launch_context&) { return false; }

    void set_app_name(const char* name)
    {
        setAppInfo().name = name;
    }

protected:
    virtual void on_create() {}
    virtual void on_open() {}
    virtual void on_running() {}
    virtual void on_sleeping() {}
    virtual void on_close() {}
    virtual void on_destroy() {}

private:
    // Mooncake owns these lifecycle entry points; Apps use snake_case adapters.
    void onCreate() final override
    {
        on_create();
    }

    void onOpen() final override
    {
        on_open();
    }

    void onRunning() final override
    {
        on_running();
    }

    void onSleeping() final override
    {
        on_sleeping();
    }

    void onClose() final override
    {
        on_close();
    }

    void onDestroy() final override
    {
        on_destroy();
    }
};
