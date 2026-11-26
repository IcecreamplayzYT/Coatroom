#pragma once

#include <string>

namespace lps {

class ObjMesh;

bool loadFbxIntoObjMesh(const std::string& path, ObjMesh& outMesh, bool normalizeScale = true);
bool writeTestBinaryFbxCar(const std::string& path);
bool writeTestAsciiFbxCar(const std::string& path);

} // namespace lps
