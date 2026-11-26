#include "RemarksPane.hpp"

#include "Common/TimeUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/box.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/util/autoreset.hpp>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace Aquarium::UI::Components {

using namespace ftxui;
using aquarium::api::CommentType;

namespace {

constexpr int kWheelScrollStep = 3;

struct RemarksScrollState {
    int offset = 0;
    int maxOffset = 0;
    int viewportHeight = 0;
    std::optional<std::int64_t> lastTaskId;
    std::size_t lastCommentCount = 0;
    std::optional<std::int64_t> lastTopCommentId;
    Box viewportBox;
};

class ScrollFrameNode : public Node {
public:
    ScrollFrameNode(Element child,
                    std::shared_ptr<RemarksScrollState> scrollState)
        : Node(Elements{std::move(child)}),
          _scrollState(std::move(scrollState)) {}

    void ComputeRequirement() override {
        Node::ComputeRequirement();
        requirement_ = children_[0]->requirement();
    }

    void SetBox(Box box) override {
        Node::SetBox(box);
        const int viewportHeight = std::max(0, box.y_max - box.y_min + 1);
        const int contentHeight = std::max(requirement_.min_y, viewportHeight);
        const int maxOffset = std::max(0, contentHeight - viewportHeight);

        _scrollState->viewportHeight = viewportHeight;
        _scrollState->maxOffset = maxOffset;
        _scrollState->offset = std::clamp(_scrollState->offset, 0, maxOffset);
        _scrollState->viewportBox = box;

        Box childBox = box;
        childBox.y_min = box.y_min - _scrollState->offset;
        childBox.y_max = childBox.y_min + contentHeight;
        children_[0]->SetBox(childBox);
    }

    void Render(Screen& screen) override {
        const AutoReset<Box> stencil(&screen.stencil,
                                     Box::Intersection(box_, screen.stencil));
        children_[0]->Render(screen);
    }

private:
    std::shared_ptr<RemarksScrollState> _scrollState;
};

Element BuildCommentElement(const State::AppState& state,
                            const aquarium::api::Comment& comment) {
    Element badge;
    const bool isRevision =
        comment.type() == CommentType::COMMENT_REVISION_REASON;

    switch (comment.type()) {
    case CommentType::COMMENT_REVISION_REASON:
        badge = text("[REVISION REASON]") | color(Color::RedLight) | bold;
        break;
    case CommentType::COMMENT_STATUS_CHANGE:
        badge = text("[STATUS CHANGE]") | color(Color::YellowLight) | bold;
        break;
    case CommentType::COMMENT_GENERAL:
    default:
        badge = text("[GENERAL]") | color(Color::CyanLight) | bold;
        break;
    }

    const auto* author = state.FindUser(comment.authorid());
    std::string authorStr = author != nullptr
                                ? author->name()
                                : "User #" + std::to_string(comment.authorid());

    std::string timeStr = Common::FormatTimestamp(comment.createdat());

    auto headerElem = hbox({
        badge,
        text(" " + authorStr) | bold,
        filler(),
        text(timeStr) | dim,
    });

    auto contentElem = paragraph(comment.content());

    auto commentBox = vbox({
        headerElem,
        contentElem,
    });

    if (isRevision) {
        return commentBox | color(Color::RedLight) | border;
    }
    return commentBox | border;
}

class RemarksFeedComponent : public ComponentBase {
public:
    RemarksFeedComponent(const State::AppState& state,
                         std::shared_ptr<RemarksScrollState> scrollState)
        : _state(state), _scrollState(std::move(scrollState)) {}

    bool Focusable() const override {
        return _state.selectedTaskId.has_value();
    }

    Element Render() override {
        SyncScrollOnDataChange();

        Elements commentElements;
        if (_state.activeComments.empty()) {
            commentElements.push_back(text("No remarks for this task yet.") |
                                      dim | center);
        } else {
            std::unordered_set<std::int64_t> seenCommentIds;
            commentElements.reserve(_state.activeComments.size());
            // Display newest remarks at the top.
            for (auto it = _state.activeComments.rbegin();
                 it != _state.activeComments.rend(); ++it) {
                const auto& comment = *it;
                if (comment.id() > 0 &&
                    !seenCommentIds.insert(comment.id()).second) {
                    continue;
                }
                commentElements.push_back(BuildCommentElement(_state, comment));
            }
        }

        auto content = vbox(std::move(commentElements)) | vscroll_indicator;
        return std::make_shared<ScrollFrameNode>(std::move(content),
                                                 _scrollState) |
               flex;
    }

    bool OnEvent(Event event) override {
        if (!_state.selectedTaskId.has_value()) {
            return false;
        }

        if (event.is_mouse()) {
            if (!_scrollState->viewportBox.Contain(event.mouse().x,
                                                   event.mouse().y)) {
                return false;
            }
            if (event.mouse().button == Mouse::WheelUp) {
                _scrollState->offset =
                    std::max(0, _scrollState->offset - kWheelScrollStep);
                return true;
            }
            if (event.mouse().button == Mouse::WheelDown) {
                _scrollState->offset =
                    std::min(_scrollState->maxOffset,
                             _scrollState->offset + kWheelScrollStep);
                return true;
            }
            if (event.mouse().button == Mouse::Left &&
                event.mouse().motion == Mouse::Pressed) {
                TakeFocus();
                return true;
            }
            return false;
        }

        if (!Focused()) {
            return false;
        }

        if (event == Event::ArrowUp || event == Event::Character('k')) {
            _scrollState->offset = std::max(0, _scrollState->offset - 1);
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
            _scrollState->offset =
                std::min(_scrollState->maxOffset, _scrollState->offset + 1);
            return true;
        }
        if (event == Event::PageUp) {
            const int step = std::max(1, _scrollState->viewportHeight - 1);
            _scrollState->offset = std::max(0, _scrollState->offset - step);
            return true;
        }
        if (event == Event::PageDown) {
            const int step = std::max(1, _scrollState->viewportHeight - 1);
            _scrollState->offset =
                std::min(_scrollState->maxOffset, _scrollState->offset + step);
            return true;
        }
        if (event == Event::Home || event == Event::Character('g')) {
            _scrollState->offset = 0;
            return true;
        }
        if (event == Event::End || event == Event::Character('G')) {
            _scrollState->offset = _scrollState->maxOffset;
            return true;
        }

        return false;
    }

private:
    void SyncScrollOnDataChange() {
        const std::optional<std::int64_t> topCommentId =
            _state.activeComments.empty()
                ? std::nullopt
                : std::make_optional(_state.activeComments.back().id());

        if (_scrollState->lastTaskId != _state.selectedTaskId) {
            _scrollState->offset = 0;
            _scrollState->lastTaskId = _state.selectedTaskId;
            _scrollState->lastCommentCount = _state.activeComments.size();
            _scrollState->lastTopCommentId = topCommentId;
            return;
        }

        if (_state.activeComments.size() != _scrollState->lastCommentCount ||
            topCommentId != _scrollState->lastTopCommentId) {
            if (_state.activeComments.size() > _scrollState->lastCommentCount ||
                (topCommentId.has_value() &&
                 topCommentId != _scrollState->lastTopCommentId)) {
                _scrollState->offset = 0;
            }
            _scrollState->lastCommentCount = _state.activeComments.size();
            _scrollState->lastTopCommentId = topCommentId;
        }
    }

    const State::AppState& _state;
    std::shared_ptr<RemarksScrollState> _scrollState;
};

} // namespace

Component CreateRemarksPane(const State::AppState& state,
                            RemarksCallbacks callbacks) {
    auto inputModel = std::make_shared<std::string>();
    auto scrollState = std::make_shared<RemarksScrollState>();

    InputOption inputOpt;
    inputOpt.multiline = false;

    auto inputField = Input(inputModel.get(), "Type remark...", inputOpt);

    auto sendAction = [&state, callbacks, inputModel, scrollState] {
        if (inputModel->empty() || !state.selectedTaskId.has_value()) {
            return;
        }
        if (callbacks.onAddComment) {
            callbacks.onAddComment(*state.selectedTaskId, *inputModel);
            inputModel->clear();
            scrollState->offset = 0;
        }
    };

    auto btnSend = Button("Send", sendAction);

    auto feedComponent = Make<RemarksFeedComponent>(state, scrollState);

    auto bottomContainer = Container::Horizontal({
        inputField,
        btnSend,
    });

    auto mainContainer = Container::Vertical({
        feedComponent,
        bottomContainer,
    });

    auto renderer =
        Renderer(mainContainer, [&state, feedComponent, inputField, btnSend] {
            if (!state.selectedTaskId.has_value()) {
                return vbox({
                           text("REMARKS & HISTORY") | bold | center,
                           separator(),
                           filler(),
                           text("No task selected.") | dim | center,
                           filler(),
                       }) |
                       border;
            }

            auto titleElem = text("REMARKS & HISTORY") | bold | center;
            if (feedComponent->Focused()) {
                titleElem = titleElem | color(Color::CyanLight);
            }

            auto inputArea = hbox({
                inputField->Render() | flex,
                separator(),
                btnSend->Render(),
            });

            return vbox({
                       titleElem,
                       separator(),
                       feedComponent->Render() | flex,
                       separator(),
                       inputArea,
                   }) |
                   border;
        });

    return CatchEvent(renderer, [sendAction, bottomContainer](Event event) {
        if (event == Event::Return && bottomContainer->Focused()) {
            sendAction();
            return true;
        }
        return false;
    });
}

} // namespace Aquarium::UI::Components
