// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#include "Resource.h"
#include "Resource_Utils.h"
#include <iostream>

namespace Resource {

    static C3Model ParseC3ModelData(const std::vector<uint8_t>& data, const std::string& debugPath) {
        C3Model model;
        if (data.empty()) {
            std::cout << "[ERRO] Nao foi possivel ler o arquivo C3: " << debugPath << "\n";
            return model;
        }
        BinaryReader br(data);
        std::string version = br.ReadString(16);
        if (version.find("MAXFILE C3") == std::string::npos) return model;
        model.isValid = true;

        while (br.CanRead(8)) {
            std::string chunkTag = br.ReadString(4);
            uint32_t chunkSize = br.Read<uint32_t>();
            size_t chunkEnd = br.GetPosition() + chunkSize;

            if (chunkTag == "PHY " || chunkTag == "PHY3" || chunkTag == "PHY4" || chunkTag == "PHY5") {
                C3Phy phy;
                uint32_t nameLen = br.Read<uint32_t>();
                phy.name = br.ReadString(nameLen);
                phy.blendCount = br.Read<uint32_t>();
                phy.normalVertexCount = br.Read<uint32_t>();
                phy.alphaVertexCount = br.Read<uint32_t>();
                int totalVerts = phy.normalVertexCount + phy.alphaVertexCount;
                int morphMax = (chunkTag == "PHY3" || chunkTag == "PHY4" || chunkTag == "PHY5") ? 1 : 4;
                for (int i = 0; i < totalVerts; i++) {
                    PhyVertex v;
                    for (int m = 0; m < morphMax; m++) {
                        float px = br.Read<float>(), py = br.Read<float>(), pz = br.Read<float>();
                        if (m == 0) { v.px = px; v.py = py; v.pz = pz; }
                    }
                    v.u = br.Read<float>(); v.v = br.Read<float>(); br.Skip(4);
                    v.boneIndex[0] = br.Read<uint32_t>(); v.boneIndex[1] = br.Read<uint32_t>();
                    v.boneWeight[0] = br.Read<float>(); v.boneWeight[1] = br.Read<float>();
                    if (chunkTag == "PHY3") br.Skip(12);
                    if (chunkTag == "PHY5") br.Skip(20);
                    phy.vertices.push_back(v);
                }
                phy.normalTriCount = br.Read<uint32_t>(); phy.alphaTriCount = br.Read<uint32_t>();
                int totalIdx = (phy.normalTriCount + phy.alphaTriCount) * 3;
                for (int i = 0; i < totalIdx; i++) phy.indices.push_back(br.Read<uint16_t>());
                uint32_t texLen = br.Read<uint32_t>(); phy.textureName = br.ReadString(texLen);
                for (int i = 0; i < 3; i++) phy.bboxMin[i] = br.Read<float>();
                for (int i = 0; i < 3; i++) phy.bboxMax[i] = br.Read<float>();
                for (int i = 0; i < 16; i++) phy.initMatrix.m[i] = br.Read<float>();
                model.phys.push_back(phy);
            }
            else if (chunkTag == "MOTI") {
                C3Motion motion;
                motion.boneCount = br.Read<uint32_t>(); motion.frameCount = br.Read<uint32_t>();
                std::string kfTag = br.ReadString(4);
                if (kfTag == "KKEY") {
                    uint32_t count = br.Read<uint32_t>();
                    for (uint32_t kk = 0; kk < count; kk++) {
                        C3KeyFrame kf; kf.pos = br.Read<uint32_t>();
                        for (uint32_t b = 0; b < motion.boneCount; b++) {
                            Matrix4x4 mat; for (int i = 0; i < 16; i++) mat.m[i] = br.Read<float>();
                            kf.boneMatrices.push_back(mat);
                        }
                        motion.keyframes.push_back(kf);
                    }
                }
                else if (kfTag == "ZKEY") {
                    uint32_t count = br.Read<uint32_t>();
                    for (uint32_t kk = 0; kk < count; kk++) {
                        C3KeyFrame kf; kf.pos = br.Read<uint16_t>();
                        for (uint32_t b = 0; b < motion.boneCount; b++) {
                            float qx = br.Read<float>(), qy = br.Read<float>(), qz = br.Read<float>(), qw = br.Read<float>();
                            float tx = br.Read<float>(), ty = br.Read<float>(), tz = br.Read<float>();
                            float xx = qx * qx, yy = qy * qy, zz = qz * qz;
                            float xy = qx * qy, zw = qz * qw, zx = qz * qx, yw = qy * qw, yz = qy * qz, xw = qx * qw;
                            Matrix4x4 mat;
                            mat.m[0] = 1.0f - 2.0f * (yy + zz); mat.m[1] = 2.0f * (xy + zw);    mat.m[2] = 2.0f * (zx - yw);    mat.m[3] = 0.0f;
                            mat.m[4] = 2.0f * (xy - zw);    mat.m[5] = 1.0f - 2.0f * (zz + xx); mat.m[6] = 2.0f * (yz + xw);    mat.m[7] = 0.0f;
                            mat.m[8] = 2.0f * (zx + yw);    mat.m[9] = 2.0f * (yz - xw);    mat.m[10] = 1.0f - 2.0f * (yy + xx); mat.m[11] = 0.0f;
                            mat.m[12] = tx;             mat.m[13] = ty;             mat.m[14] = tz;             mat.m[15] = 1.0f;
                            kf.boneMatrices.push_back(mat);
                        }
                        motion.keyframes.push_back(kf);
                    }
                }
                else if (kfTag == "XKEY") {
                    uint32_t count = br.Read<uint32_t>();
                    for (uint32_t kk = 0; kk < count; kk++) {
                        C3KeyFrame kf; kf.pos = br.Read<uint16_t>();
                        for (uint32_t b = 0; b < motion.boneCount; b++) {
                            Matrix4x4 mat;
                            mat.m[0] = br.Read<float>(); mat.m[1] = br.Read<float>(); mat.m[2] = br.Read<float>(); mat.m[3] = 0.0f;
                            mat.m[4] = br.Read<float>(); mat.m[5] = br.Read<float>(); mat.m[6] = br.Read<float>(); mat.m[7] = 0.0f;
                            mat.m[8] = br.Read<float>(); mat.m[9] = br.Read<float>(); mat.m[10] = br.Read<float>(); mat.m[11] = 0.0f;
                            mat.m[12] = br.Read<float>(); mat.m[13] = br.Read<float>(); mat.m[14] = br.Read<float>(); mat.m[15] = 1.0f;
                            kf.boneMatrices.push_back(mat);
                        }
                        motion.keyframes.push_back(kf);
                    }
                }
                else {
                    br.SkipBack(4);
                    for (int kk = 0; kk < motion.frameCount; kk++) {
                        C3KeyFrame kf; kf.pos = kk;
                        for (uint32_t b = 0; b < motion.boneCount; b++) {
                            Matrix4x4 mat;
                            for (int i = 0; i < 16; i++) mat.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
                            kf.boneMatrices.push_back(mat);
                        }
                        motion.keyframes.push_back(kf);
                    }
                    for (uint32_t b = 0; b < motion.boneCount; b++) {
                        for (int kk = 0; kk < motion.frameCount; kk++) {
                            for (int i = 0; i < 16; i++) motion.keyframes[kk].boneMatrices[b].m[i] = br.Read<float>();
                        }
                    }
                }
                model.motions.push_back(motion);
            }
            else if (chunkTag == "PTCL" || chunkTag == "PTC3" || chunkTag == "PTCL3" || chunkTag == "PTCX") {
                C3Ptcl ptcl;
                ptcl.isPTC3 = (chunkTag != "PTCL");

                uint32_t nameLen = br.Read<uint32_t>();
                ptcl.name = br.ReadString(nameLen);

                uint32_t texLen = br.Read<uint32_t>();
                ptcl.textureName = br.ReadString(texLen);
                ptcl.texRow = br.Read<uint32_t>();

                if (ptcl.isPTC3) {
                    br.Skip(2);
                    br.Skip(8);
                    ptcl.scaleX = br.Read<float>();
                    ptcl.scaleY = br.Read<float>();
                    ptcl.scaleZ = br.Read<float>();
                    br.Skip(8);
                    ptcl.rotationSpeed = br.Read<float>() * 180.0f / 3.14159f;
                    ptcl.maxAlpha = br.Read<float>();
                    ptcl.minAlpha = br.Read<float>();
                    ptcl.totalLifetime = br.Read<float>();
                    ptcl.fadeStartAge = br.Read<float>();
                    ptcl.fadeEndAge = ptcl.totalLifetime * 0.8f;
                }

                ptcl.maxCount = br.Read<uint32_t>();
                uint32_t frameCount = br.Read<uint32_t>();

                for (uint32_t n = 0; n < frameCount; n++) {
                    PtclFrame frame;
                    uint32_t count = br.Read<uint32_t>();
                    if (count > 0) {
                        if (ptcl.isPTC3) br.Skip(count * 2);

                        for (uint32_t i = 0; i < count; i++) {
                            Vec3 pos; pos.x = br.Read<float>(); pos.y = br.Read<float>(); pos.z = br.Read<float>();
                            frame.positions.push_back(pos);
                        }
                        for (uint32_t i = 0; i < count; i++) frame.ages.push_back(br.Read<float>());
                        for (uint32_t i = 0; i < count; i++) frame.sizes.push_back(br.Read<float>());
                        for (int i = 0; i < 16; i++) frame.frameMatrix.m[i] = br.Read<float>();
                    }
                    ptcl.frames.push_back(frame);
                }
                model.ptcls.push_back(ptcl);
            }
            else if (chunkTag == "SHAP") {
                std::cout << "[C3 PARSER] -> Bloco de Rastro/Shape (" << chunkTag << ") encontrado! (Requer sistema de ribbons)\n";
            }

            br.SetPosition(chunkEnd);
        }
        return model;
    }
}