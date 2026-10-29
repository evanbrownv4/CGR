// Parser for a subset of the PBRT (v4, with some v3 compatibility) scene format.
#pragma once

#include <memory>
#include <string>

#include "logger.h"
#include "scene.h"

// Parses `path` into `scene`. Syntax errors and unsupported features are
// reported through `log` and skipped where possible; returns false only when
// the scene could not be read at all (e.g. the file does not exist).
bool parseSceneFile(const std::string& path, Scene& scene, Logger& log);

// Loads a Wavefront OBJ file into a mesh (object space). Returns false on fatal error.
bool loadOBJ(const std::string& path, Mesh& mesh, bool computeSmoothNormals, Logger& log);
