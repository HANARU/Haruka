// miniaudio 와 stb_vorbis 의 구현부만 담는 번역 단위입니다.
//
// AudioSystem.cpp 에서 떼어 낸 이유는 둘입니다. 구현부가 9만 줄이 넘어 AudioSystem.cpp 를
// 고칠 때마다 다시 컴파일하지 않게 하고, 오디오 테스트(HarukaAudioTest)가 같은 구현을
// 그대로 링크하게 합니다.
//
// stb_vorbis 를 이 순서로 넣으면 miniaudio 가 OGG Vorbis 디코더를 켭니다. 선언부가 먼저
// 있어야 miniaudio 구현부가 디코더를 등록하고(STB_VORBIS_INCLUDE_STB_VORBIS_H 를 봅니다),
// stb_vorbis 구현부는 miniaudio 뒤에 와야 합니다. 예전에는 이 줄이 없어서 콘텐츠 브라우저가
// .ogg 를 보여 주는데도 불러오기는 항상 실패했습니다. 두 파일 모두 miniaudio 저장소의
// extras 에 있는 것을 써서, 버전이 miniaudio 태그(CMakeLists.txt)와 함께 고정됩니다.
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// stb_vorbis 구현부는 /W4 에서 좁히는 변환(C4244) 등 경고를 70건 넘게 냅니다. 고치지 않는
// 외부 코드라 이 구간에서만 경고를 끕니다(miniaudio 구현부는 /W4 에서도 경고가 없습니다).
// C4701 은 코드 생성 단계의 경고라 push(0) 만으로는 꺼지지 않아 번호를 따로 끕니다.
#undef STB_VORBIS_HEADER_ONLY
#if defined(_MSC_VER)
    #pragma warning(push, 0)
    #pragma warning(disable: 4244 4245 4701 4703)
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Weverything"
#endif
#include "extras/stb_vorbis.c"
#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#endif
