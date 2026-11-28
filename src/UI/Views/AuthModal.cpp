#include "AuthModal.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <utility>

namespace Aquarium::UI::Views {

using namespace ftxui;

namespace {

struct AuthModalState {
    std::string server;
    std::string name;
    std::string inviteKey;
    std::string error;
};

} // namespace

Component CreateAuthModal(const State::AppState& state,
                          AuthModalCallbacks callbacks) {
    auto form = std::make_shared<AuthModalState>();
    form->server = state.session.serverHost.empty() ? "localhost:50051"
                                                    : state.session.serverHost;

    InputOption inputOpt;
    inputOpt.multiline = false;

    auto serverInput = Input(&form->server, "localhost:50051", inputOpt);
    auto nameInput = Input(&form->name, "Your username", inputOpt);

    InputOption secretOpt = inputOpt;
    secretOpt.password = true;
    auto inviteInput = Input(&form->inviteKey, "Secret invite key", secretOpt);

    auto cb = std::make_shared<AuthModalCallbacks>(std::move(callbacks));

    auto trySubmit = [cb, form] {
        if (form->server.empty()) {
            form->error = "Server address cannot be empty.";
            return;
        }
        if (form->name.empty()) {
            form->error = "Username cannot be empty.";
            return;
        }
        if (form->inviteKey.empty()) {
            form->error = "Invite key cannot be empty.";
            return;
        }
        form->error.clear();
        if (cb->onSubmit) {
            cb->onSubmit(form->server, form->name, form->inviteKey);
        }
    };

    auto btnSubmit = Button("Register & Connect", trySubmit);
    auto btnCancel = Button("Cancel", [cb] {
        if (cb->onCancel) {
            cb->onCancel();
        }
    });

    auto container = Container::Vertical({
        serverInput,
        nameInput,
        inviteInput,
        Container::Horizontal({btnSubmit, btnCancel}),
    });

    auto renderer = Renderer(container, [serverInput, nameInput, inviteInput,
                                         btnSubmit, btnCancel, form] {
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

        if (!form->error.empty()) {
            formRows.push_back(separator());
            formRows.push_back(text(form->error) | color(Color::RedLight) |
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

    return CatchEvent(renderer, [trySubmit, cb](Event event) {
        if (event == Event::Return) {
            trySubmit();
            return true;
        }
        if (event == Event::Escape) {
            if (cb->onCancel) {
                cb->onCancel();
            }
            return true;
        }
        return false;
    });
}

} // namespace Aquarium::UI::Views
