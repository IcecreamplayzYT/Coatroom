#pragma once

#include "ui/ui_types.hpp"

namespace lps {

void drawTopWorkspaceBar(StudioUiState& state, UvViewportState& uvState,
                         CanvasEngine& canvas, ObjMesh& mesh,
                         const StudioCallbacks& callbacks,
                         float x, float y, float width, float height);

void drawToolRail(StudioUiState& state, const StudioCallbacks& callbacks,
                  float x, float y, float width, float height,
                  CanvasEngine* canvas = nullptr, const ObjMesh* mesh = nullptr);

void drawRightIconRail(StudioUiState& state,
                       float x, float y, float width, float height);

void drawViewport3DPanel(StudioUiState& state, const ObjMesh& mesh,
                         const StudioCallbacks& callbacks);

void drawLayersPanel(StudioUiState& state, CanvasEngine& canvas,
                     const ObjMesh& mesh, const StudioCallbacks& callbacks);

void drawInspectorPanel(StudioUiState& state, CanvasEngine& canvas,
                        ObjMesh& mesh, const StudioCallbacks& callbacks);

void drawStatusBar(const StudioUiState& state, const CanvasEngine& canvas,
                   const ObjMesh& mesh, float x, float y, float width, float height);

// Full unified studio layout (Menu Bar + Control Strip + 2-Col Illustrator Tool Dock + Docked 3D/2D/Color/Properties/Layers + Status Bar)
void drawStudioWorkspace(StudioUiState& state, UvViewportState& uvState,
                         CanvasEngine& canvas, ObjMesh& mesh,
                         const StudioCallbacks& callbacks);

} // namespace lps
