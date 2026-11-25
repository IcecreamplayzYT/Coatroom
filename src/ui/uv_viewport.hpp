#pragma once

#include "ui/ui_types.hpp"

namespace lps {

void uvFitToView(UvViewportState& uvState, float viewportW, float viewportH);

// Renders the interactive 2D UV Canvas inside the current ImGui window
void drawUvViewport(StudioUiState& state, UvViewportState& uvState,
                    CanvasEngine& canvas, const ObjMesh& mesh,
                    const StudioCallbacks& callbacks);

} // namespace lps
