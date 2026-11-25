#include "RemarksPane.hpp"

#include "Common/TimeUtils.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <string>
#include <unordered_set>
#include <vector>

namespace Aquarium::UI::Components {

using namespace ftxui;
using aquarium::api::CommentType;

Component CreateRemarksPane(const State::AppState& state,
                            RemarksCallbacks callbacks) {
    auto inputModel = std::make_shared<std::string>();

    InputOption inputOpt;
    inputOpt.multiline = false;

    auto inputField = Input(inputModel.get(), "Type remark...", inputOpt);

    auto sendAction = [&state, callbacks, inputModel] {
        if (inputModel->empty() || !state.selectedTaskId.has_value()) {
            return;
        }
        if (callbacks.onAddComment) {
            callbacks.onAddComment(*state.selectedTaskId, *inputModel);
            inputModel->clear();
        }
    };

    auto btnSend = Button("Send", sendAction);

    auto bottomContainer = Container::Horizontal({
        inputField,
        btnSend,
    });

    auto renderer = Renderer(bottomContainer, [&state, bottomContainer,
                                               inputField, btnSend] {
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

        Elements commentElements;
        if (state.activeComments.empty()) {
            commentElements.push_back(text("No remarks for this task yet.") |
                                      dim | center);
        } else {
            std::unordered_set<std::int64_t> seenCommentIds;
            for (const auto& comment : state.activeComments) {
                if (comment.id() > 0 &&
                    !seenCommentIds.insert(comment.id()).second) {
                    continue;
                }
                Element badge;
                bool isRevision =
                    comment.type() == CommentType::COMMENT_REVISION_REASON;

                switch (comment.type()) {
                case CommentType::COMMENT_REVISION_REASON:
                    badge = text("[REVISION REASON]") | color(Color::RedLight) |
                            bold;
                    break;
                case CommentType::COMMENT_STATUS_CHANGE:
                    badge = text("[STATUS CHANGE]") |
                            color(Color::YellowLight) | bold;
                    break;
                case CommentType::COMMENT_GENERAL:
                default:
                    badge = text("[GENERAL]") | color(Color::CyanLight) | bold;
                    break;
                }

                auto author = state.FindUser(comment.authorid());
                std::string authorStr =
                    author != nullptr
                        ? author->name()
                        : "User #" + std::to_string(comment.authorid());

                std::string timeStr =
                    Common::FormatTimestamp(comment.createdat());

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
                    commentElements.push_back(commentBox |
                                              color(Color::RedLight) | border);
                } else {
                    commentElements.push_back(commentBox | border);
                }
            }
        }

        auto feedArea = vbox(std::move(commentElements)) | yframe | flex;

        auto inputArea = hbox({
            inputField->Render() | flex,
            separator(),
            btnSend->Render(),
        });

        return vbox({
                   text("REMARKS & HISTORY") | bold | center,
                   separator(),
                   feedArea,
                   separator(),
                   inputArea,
               }) |
               border;
    });

    return CatchEvent(renderer, [sendAction](Event event) {
        if (event == Event::Return) {
            sendAction();
            return true;
        }
        return false;
    });
}

} // namespace Aquarium::UI::Components
