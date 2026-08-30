#pragma once

#include <mooncake.h>

#include "app_event.hpp"

class app_base : public mooncake::AppAbility {
public:
    virtual void handle_app_event(const app_event& event) = 0;

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
