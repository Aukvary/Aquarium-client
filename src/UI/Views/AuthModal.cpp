#include "AuthModal.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <utility>

namespace Aquarium::UI::Views {

using namespace ftxui;

Component CreateAuthModal(const State::AppState& state,
                          AuthModalCallbacks callbacks) {
    auto serverModel = std::make_shared<std::string>(
        state.session.serverHost.empty() ? "localhost:50051"
                                         : state.session.serverHost);
    auto nameModel = std::make_shared<std::string>();
    auto inviteKeyModel = std::make_shared<std::string>();
    auto errorModel = std::make_shared<std::string>();

    InputOption inputOpt;
    inputOpt.multiline = false;

    auto serverInput = Input(serverModel.get(), "localhost:50051", inputOpt);
    auto nameInput = Input(nameModel.get(), "Your username", inputOpt);

    InputOption secretOpt = inputOpt;
    secretOpt.password = true;
    auto inviteInput =
        Input(inviteKeyModel.get(), "Secret invite key", secretOpt);

    auto trySubmit = [callbacks, serverModel, nameModel, inviteKeyModel,
                      errorModel] {
        if (serverModel->empty()) {
            *errorModel = "Server address cannot be empty.";
            return;
        }
        if (nameModel->empty()) {
            *errorModel = "Username cannot be empty.";
            return;
        }
        if (inviteKeyModel->empty()) {
            *errorModel = "Invite key cannot be empty.";
            return;
        }
        errorModel->clear();
        if (callbacks.onSubmit) {
            callbacks.onSubmit(*serverModel, *nameModel, *inviteKeyModel);
        }
    };

    auto btnSubmit = Button("Register & Connect", trySubmit);
    auto btnCancel = Button("Cancel", [callbacks] {
        if (callbacks.onCancel) {
            callbacks.onCancel();
        }
    });

    auto container = Container::Vertical({
        serverInput,
        nameInput,
        inviteInput,
        Container::Horizontal({btnSubmit, btnCancel}),
    });

    auto renderer = Renderer(container, [serverInput, nameInput, inviteInput,
                                         btnSubmit, btnCancel, errorModel] {
        Elements formRows = {
            text("REGISTRATION / LOGIN") | bold | color(Color::CyanLight) |
                center,
            separator(),
            text("Enter server and credentials below:") | dim,
            separator(),
            hbox({
                text("Server:    ") | bold,
                serverInput->Render() | flex,
            }),
            hbox({
                text("Username:  ") | bold,
                nameInput->Render() | flex,
            }),
            hbox({
                text("Invite Key: ") | bold,
                inviteInput->Render() | flex,
            }),
        };

        if (!errorModel->empty()) {
            formRows.push_back(separator());
            formRows.push_back(text(*errorModel) | color(Color::RedLight) |
                               bold | center);
        }

        formRows.push_back(separator());
        formRows.push_back(hbox({
            filler(),
            btnSubmit->Render(),
            text("  "),
            btnCancel->Render(),
            filler(),
        }));

        return vbox(std::move(formRows)) | border | size(WIDTH, EQUAL, 52) |
               clear_under;
    });

    return CatchEvent(renderer, [trySubmit, callbacks](Event event) {
        if (event == Event::Return) {
            trySubmit();
            return true;
        }
        if (event == Event::Escape) {
            if (callbacks.onCancel) {
                callbacks.onCancel();
            }
            return true;
        }
        return false;
    });
}

} // namespace Aquarium::UI::Views
