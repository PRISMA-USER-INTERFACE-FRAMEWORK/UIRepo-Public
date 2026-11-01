#include "uirepo/Core.h"

namespace uirepo {
void Node::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    if (style_.background.a != 0) {
        commands.emplace_back(FillRectCommand{
            id_, bounds_, style_.opacity, style_.zIndex, style_.background, style_.cornerRadius});
    }
}
void TextNode::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    Node::appendDrawCommands(commands);
    commands.emplace_back(DrawTextCommand{
        id(), bounds().deflate(style().padding), style().opacity, style().zIndex,
        text_, fontSize_, color_, textLayout_});
}

void ButtonNode::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    const Color background = focused() ? focusedBackground_ : style().background;
    if (background.a != 0) {
        commands.emplace_back(FillRectCommand{
            id(), bounds(), style().opacity, style().zIndex, background, style().cornerRadius});
    }

    commands.emplace_back(DrawTextCommand{
        id(), bounds().deflate(style().padding), style().opacity, style().zIndex,
        text_, fontSize_, textColor_, textLayout_});
}

void ImageNode::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    Node::appendDrawCommands(commands);
    commands.emplace_back(DrawImageCommand{
        id(), bounds().deflate(style().padding), style().opacity, style().zIndex, source_});
}

void VideoNode::appendDrawCommands(std::vector<DrawCommand>& commands) const {
    Node::appendDrawCommands(commands);
    commands.emplace_back(DrawVideoCommand{
        id(), bounds().deflate(style().padding), style().opacity, style().zIndex,
        source_, playbackState_, loop_, volume_});
}

}
