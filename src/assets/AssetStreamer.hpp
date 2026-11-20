#pragma once
// 백그라운드 모델 읽기(2026-11-20, Docs/Async_Loading.md D1). 워커 스레드가 VFS 읽기 · 캐시(.gasset) 읽기 또는 glTF 디코드 ·
// 캐시 쓰기만 하고, 결과(MeshSourceData 목록)를 뮤텍스로 지킨 표에 둡니다. GPU 업로드 · World 는 메인 스레드의 AssetManager 가
// 합니다(Threading.md 규칙 1 ~ 3). 같은 경로를 다시 요청하면 앞 작업을 함께 씁니다.

#include "../geometry/MeshSource.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class AssetStreamer {
public:
    enum class State { None, Queued, Loading, Done };

    struct Result {
        std::string                 path;     // 정식 논리 경로
        bool                        ok = false;
        std::vector<MeshSourceData> meshes;   // ok 가 아니면 비어 있음(부르는 쪽이 기본 상자를 씀)
        std::string                 error;
    };

    explicit AssetStreamer(int workers);
    ~AssetStreamer();   // 대기 중 작업은 버리고, 읽는 중인 작업은 끝날 때까지 기다린 뒤 스레드를 거둡니다
    AssetStreamer(const AssetStreamer&) = delete;
    AssetStreamer& operator=(const AssetStreamer&) = delete;

    // 정식 논리 경로를 요청합니다. 이미 대기 · 읽는 중 · 끝난 결과가 있으면 false(새 작업 없음).
    bool  request(const std::string& path);
    State state(const std::string& path) const;
    // 끝난 결과를 꺼냅니다(꺼낸 것은 표에서 사라짐). max 개까지.
    std::vector<Result> takeFinished(size_t max = (size_t)-1);
    // 그 경로의 작업이 끝날 때까지 기다려 결과를 꺼냅니다. 요청한 적이 없으면 false.
    bool  waitFor(const std::string& path, Result& out);
    size_t inFlight() const;   // 대기 + 읽는 중
    size_t pending() const;    // 대기 + 읽는 중 + 끝났지만 아직 꺼내지 않음
    int    workers() const { return (int)threads_.size(); }

    // 모델 하나를 읽습니다(워커 · 동기 경로 공용): 캐시가 맞으면 캐시, 아니면 glTF 를 읽고 캐시에 씁니다. 실패하면 false.
    static bool decode(const std::string& path, std::vector<MeshSourceData>& out, std::string& error);

private:
    void run();

    mutable std::mutex                       mu_;
    std::condition_variable                  cv_;      // 새 작업 · 멈춤 → 워커
    std::condition_variable                  doneCv_;  // 작업 끝남 → waitFor
    std::deque<std::string>                  queue_;   // mu_
    std::unordered_map<std::string, State>   states_;  // mu_
    std::unordered_map<std::string, Result>  done_;    // mu_
    bool                                     stop_ = false;   // mu_
    std::vector<std::thread>                 threads_;
};
