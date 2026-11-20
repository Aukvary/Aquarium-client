#include "NavigationSidebar.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <string>
#include <vector>

namespace Aquarium::UI {

using namespace ftxui;
using State::ViewType;

Component CreateNavigationSidebar(const State::AppState& state,
                                  std::function<void(ViewType)> onNavigate) {
    auto opt = ButtonOption::Simple();

    auto btnTasks = Button(
        " [1] Tasks", [onNavigate] { onNavigate(ViewType::Tasks); }, opt);

    auto btnReview = Button(
        " [2] Review", [onNavigate] { onNavigate(ViewType::Review); }, opt);

    auto btnCreate = Button(
        " [3] Create", [onNavigate] { onNavigate(ViewType::CreateTask); }, opt);

    auto container = Container::Vertical({
        btnTasks,
        btnReview,
        btnCreate,
    });

    auto renderer = Renderer(container, [&state, btnTasks, btnReview,
                                         btnCreate] {
        auto renderItem = [](const Component& comp, bool isActive) {
            auto elem = comp->Render();
            if (isActive) {
                return elem | color(Color::CyanLight) | bold | inverted;
            }
            return elem;
        };

        return vbox({
                   text("NAVIGATION") | bold | center,
                   separator(),
                   renderItem(btnTasks, state.currentView == ViewType::Tasks),
                   renderItem(btnReview, state.currentView == ViewType::Review),
                   renderItem(btnCreate,
                              state.currentView == ViewType::CreateTask),
                   filler(),
               }) |
               border | size(WIDTH, EQUAL, 16);
    });

    return CatchEvent(renderer, [onNavigate](Event event) {
        if (event == Event::Character('1')) {
            onNavigate(ViewType::Tasks);
            return true;
        }
        if (event == Event::Character('2')) {
            onNavigate(ViewType::Review);
            return true;
        }
        if (event == Event::Character('3')) {
            onNavigate(ViewType::CreateTask);
            return true;
        }
        return false;
    });
}

} // namespace Aquarium::UI
