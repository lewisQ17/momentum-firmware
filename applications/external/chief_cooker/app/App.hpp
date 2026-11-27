#pragma once

#include "lib/ui/UiManager.hpp"
#include "lib/hardware/notification/Notification.hpp"
#include "lib/hardware/subghz/FrequencyManager.hpp"

#include "AppConfig.hpp"
#include "app/screen/MainMenuScreen.hpp"

using namespace std;

class App {
public:
    void Run() {
        UiManager* ui = UiManager::GetInstance();
        ui->InitGui();

        FrequencyManager* frequencyManager = FrequencyManager::GetInstance();
        AppConfig* config = new AppConfig();
        config->Load();

        MainMenuScreen* mainMenuScreen = new MainMenuScreen(config);
        ui->PushView(mainMenuScreen->GetView());
        ui->RunEventLoop();

        // delete ui first: ~UiManager pops and destroys any views still on the stack
        // (now that RunEventLoop no longer does this itself on loader/RPC close), which
        // stops SubGhzModule and turns off OTG; those views can still reference
        // frequencyManager/config while being torn down, so those must outlive this call.
        // mainMenuScreen is not deleted here: the menu view's own on-destroy handler
        // already does `delete this` on it when ~UiManager pops the view, so a second
        // delete here would be a double free.
        delete ui;
        delete frequencyManager;
        delete config;

        Notification::Dispose();
    }
};
