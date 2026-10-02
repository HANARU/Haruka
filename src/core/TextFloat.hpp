#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

// float 를 "다시 읽으면 같은 값이 되는 가장 짧은 십진수"로 씁니다. 씬·프리팹 파일처럼
// 저장했다가 다시 읽는 텍스트에 씁니다.
//
// iostream 기본값(유효 숫자 6자리)은 1.2345678 을 1.23457 로 잘라, 저장과 불러오기를
// 거칠 때마다 값이 바뀝니다. 9자리로 고정하면 값은 지켜지지만 0.02 가 0.0199999996 으로
// 보여 손으로 읽고 고치기 어렵습니다. 그래서 6자리부터 늘려 가며 되읽어 같은 첫 자리수를
// 씁니다(float 는 9자리면 항상 왕복합니다). -0 은 0 으로 씁니다.
inline std::string floatToText(float v) {
    if (v == 0.f) return "0";
    char buf[32];
    for (int prec = 6; prec <= 9; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, (double)v);
        if (std::strtof(buf, nullptr) == v) break;
    }
    return buf;
}
