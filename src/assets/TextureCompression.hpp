#pragma once
#include "../geometry/MeshSource.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ── 텍스처 압축: 형식 표 · 열쇠 · 미리 압축한 KTX2 찾기 (2026-11-21, Docs/Texture_Compression.md) ──────────────
// 묶음 도구(HarukaPak, 인코더가 있음)와 엔진(인코더 없음)이 함께 씁니다. 같은 화소에서 같은 열쇠가 나와야 묶을 때 만든 KTX2 를
// 실행할 때 찾습니다(D5). 열쇠가 맞지 않거나 파일이 없거나 손상되면 붙이지 않고, 엔진은 RGBA8 원본을 올립니다.
namespace htex {

enum class Target : uint8_t { Off, BC, ASTC };
// 용도(D2): 색 = albedo · emissive(sRGB), 노멀 = 두 채널, 선형 = metalRough · ao.
enum class Use : uint8_t { Color, Normal, Linear };

// 압축 방식(형식 · 품질 · 밉 필터)을 바꾸면 올립니다. 열쇠에 섞여 옛 캐시 · 묶음의 KTX2 를 찾지 않게 됩니다.
constexpr uint32_t kCookVersion = 1;

const char* targetName(Target t);                    // "off" · "bc" · "astc"
bool        parseTarget(std::string_view s, Target& out);   // off · bc · astc (대소문자 무시)
const char* useName(Use u);                          // "color" · "normal" · "linear"

// 용도 × 대상 → VkFormat 번호(D2). Off 면 0.
uint32_t formatFor(Target t, Use u);
// 압축할 크기인지. 한 변이 4보다 작으면(팩터만 있는 1x1 등) RGBA8 그대로 둡니다.
bool     eligible(int w, int h);

// 다섯 칸(albedo · normal · metalRough · ao · emissive)의 용도.
enum Slot : uint8_t { kAlbedo, kNormal, kMetalRough, kAo, kEmissive, kSlotCount };
Use  slotUse(int slot);
TexSourceData&       slotTex(MeshSourceData& m, int slot);
const TexSourceData& slotTex(const MeshSourceData& m, int slot);

// 최종 RGBA8 화소(팩터 · 가져오기 설정 적용 뒤) + 크기 + 용도 + kCookVersion 의 64비트 열쇠.
uint64_t    textureKey(const uint8_t* rgba, int w, int h, Use u);
// 묶음 · 쿠킹 폴더 안의 논리 경로: "textures/<bc|astc>/<열쇠 16자리>.ktx2".
std::string ktx2Path(Target t, uint64_t key);

struct AttachStats {
    int found   = 0;   // 붙임
    int missing = 0;   // 압축할 크기인데 파일이 없음(묶을 때 빠졌거나 원본이 바뀜)
    int invalid = 0;   // 파일은 있지만 읽을 수 없거나 형식 · 크기가 맞지 않음
};
// 메시마다 압축할 크기의 텍스처에 KTX2 를 찾아 TexSourceData::compressed 에 붙입니다(VFS 로 읽음, 워커에서 부름). Off 면 아무것도
// 하지 않습니다. 파일의 형식이 표와 다르거나 크기가 원본과 다르면 붙이지 않습니다(invalid).
AttachStats attachCompressed(std::vector<MeshSourceData>& meshes, Target t);

} // namespace htex
