#include "pch.h"
// ============================================================================
// Conquer Kayank Engine
// ============================================================================

#ifndef CORE_EXPORTS
#define CORE_EXPORTS
#endif

#ifdef CORE_EXPORTS
#define CORE_API __declspec(dllexport)
#else
#define CORE_API __declspec(dllimport)
#endif

#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <filesystem>
#include <any>
#include <typeindex>

#include <DirectXMath.h>
#include <DirectXCollision.h>

namespace Core {
    enum class LogLevel {
        INFO,
        WARNING,
        ERR,
        FATAL
    };

    class CORE_API Logger {
    public:
        static Logger& GetInstance() {
            static Logger instance;
            return instance;
        }

        void Log(LogLevel level, const std::string& message) {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::string prefix = "[INFO] ";
            if (level == LogLevel::WARNING) prefix = "[WARN] ";
            else if (level == LogLevel::ERR) prefix = "[ERROR] ";
            else if (level == LogLevel::FATAL) prefix = "[FATAL] ";

            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);

            std::string logMsg = prefix + message + "\n";
            std::cout << logMsg;

            if (m_fileStream.is_open()) {
                m_fileStream << logMsg;
                m_fileStream.flush();
            }
        }

        void Initialize(const std::string& filepath) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_fileStream.open(filepath, std::ios::out | std::ios::app);
        }

    private:
        Logger() = default;
        ~Logger() { if (m_fileStream.is_open()) m_fileStream.close(); }
        std::mutex m_mutex;
        std::ofstream m_fileStream;
    };

#define LOG_INFO(msg)  Core::Logger::GetInstance().Log(Core::LogLevel::INFO, msg)
#define LOG_WARN(msg)  Core::Logger::GetInstance().Log(Core::LogLevel::WARNING, msg)
#define LOG_ERROR(msg) Core::Logger::GetInstance().Log(Core::LogLevel::ERR, msg)


    namespace Math {

        struct CORE_API Vec2 {
            float x, y;
            Vec2() : x(0), y(0) {}
            Vec2(float x, float y) : x(x), y(y) {}
        };

        struct CORE_API Vec3 {
            float x, y, z;
            Vec3() : x(0), y(0), z(0) {}
            Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

            DirectX::XMVECTOR ToXMVECTOR() const {
                return DirectX::XMVectorSet(x, y, z, 0.0f);
            }
            static Vec3 FromXMVECTOR(DirectX::XMVECTOR v) {
                DirectX::XMFLOAT3 temp;
                DirectX::XMStoreFloat3(&temp, v);
                return Vec3(temp.x, temp.y, temp.z);
            }
        };

        struct CORE_API Quaternion {
            float x, y, z, w;
            Quaternion() : x(0), y(0), z(0), w(1) {}
            Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
        };

        struct CORE_API Matrix {
            DirectX::XMFLOAT4X4 m;

            Matrix() {
                DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixIdentity());
            }

            DirectX::XMMATRIX ToXMMATRIX() const {
                return DirectX::XMLoadFloat4x4(&m);
            }

            static Matrix CreateTranslation(const Vec3& position) {
                Matrix mat;
                DirectX::XMMATRIX trans = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
                DirectX::XMStoreFloat4x4(&mat.m, trans);
                return mat;
            }
        };

        struct CORE_API BBox {
            Vec3 center;
            Vec3 extents;

            DirectX::BoundingBox ToDirectX() const {
                return DirectX::BoundingBox(
                    DirectX::XMFLOAT3(center.x, center.y, center.z),
                    DirectX::XMFLOAT3(extents.x, extents.y, extents.z)
                );
            }
        };

        struct CORE_API Frustum {
            DirectX::BoundingFrustum dxFrustum;

            void Update(const Matrix& projection) {
                DirectX::BoundingFrustum::CreateFromMatrix(dxFrustum, projection.ToXMMATRIX());
            }

            bool Intersects(const BBox& box) const {
                return dxFrustum.Intersects(box.ToDirectX());
            }
        };
    }

    class CORE_API JobSystem {
    public:
        static JobSystem& GetInstance() {
            static JobSystem instance;
            return instance;
        }

        void Initialize(size_t numThreads = std::thread::hardware_concurrency()) {
            m_stop = false;
            for (size_t i = 0; i < numThreads; ++i) {
                m_workers.emplace_back([this]() {
                    while (true) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(m_queueMutex);
                            m_condition.wait(lock, [this] { return m_stop || !m_tasks.empty(); });

                            if (m_stop && m_tasks.empty()) return;

                            task = std::move(m_tasks.front());
                            m_tasks.pop();
                        }
                        task();
                    }
                    });
            }
            LOG_INFO("JobSystem inicializado com " + std::to_string(numThreads) + " threads.");
        }

        void Shutdown() {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_stop = true;
            }
            m_condition.notify_all();
            for (std::thread& worker : m_workers) {
                if (worker.joinable()) worker.join();
            }
            m_workers.clear();
        }

        void Execute(std::function<void()> task) {
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_tasks.emplace(std::move(task));
            }
            m_condition.notify_one();
        }

    private:
        JobSystem() : m_stop(false) {}
        ~JobSystem() { Shutdown(); }

        std::vector<std::thread> m_workers;
        std::queue<std::function<void()>> m_tasks;
        std::mutex m_queueMutex;
        std::condition_variable m_condition;
        bool m_stop;
    };

    class CORE_API FileSystem {
    public:
        static bool Exists(const std::string& path) {
            return std::filesystem::exists(path);
        }

        static std::vector<uint8_t> ReadAllBytes(const std::string& filepath) {
            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                LOG_ERROR("Falha ao abrir arquivo: " + filepath);
                return {};
            }
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            std::vector<uint8_t> buffer(size);
            if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
                return buffer;
            }
            return {};
        }
    };

    class CORE_API Configuration {
    public:
        void SetString(const std::string& key, const std::string& value) {
            m_configs[key] = value;
        }

        std::string GetString(const std::string& key, const std::string& defaultVal = "") {
            auto it = m_configs.find(key);
            return (it != m_configs.end()) ? it->second : defaultVal;
        }

    private:
        std::unordered_map<std::string, std::string> m_configs;
    };

    template<typename T>
    class CORE_API MemoryPool {
    public:
        MemoryPool(size_t initialCapacity = 100) {
            for (size_t i = 0; i < initialCapacity; ++i) {
                m_pool.push(std::make_unique<T>());
            }
        }

        std::unique_ptr<T> Acquire() {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_pool.empty()) {
                return std::make_unique<T>();
            }
            auto obj = std::move(m_pool.front());
            m_pool.pop();
            return obj;
        }

        void Release(std::unique_ptr<T> obj) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pool.push(std::move(obj));
        }

    private:
        std::queue<std::unique_ptr<T>> m_pool;
        std::mutex m_mutex;
    };

    class CORE_API EventDispatcher {
    public:
        using EventCallback = std::function<void(const std::any&)>;

        void Subscribe(std::type_index type, EventCallback callback) {
            m_listeners[type].push_back(callback);
        }

        template<typename EventType>
        void Publish(const EventType& ev) {
            auto it = m_listeners.find(typeid(EventType));
            if (it != m_listeners.end()) {
                for (auto& callback : it->second) {
                    callback(ev); 
                }
            }
        }

    private:
        std::unordered_map<std::type_index, std::vector<EventCallback>> m_listeners;
    };
    enum class KeyCode {
        W, A, S, D, SPACE, ESC, ENTER
    };

    class CORE_API InputManager {
    public:
        static InputManager& GetInstance() {
            static InputManager instance;
            return instance;
        }

        void SetKeyDown(KeyCode key, bool isDown) {
            m_keyStates[key] = isDown;
        }

        bool IsKeyDown(KeyCode key) const {
            auto it = m_keyStates.find(key);
            return (it != m_keyStates.end()) ? it->second : false;
        }

    private:
        std::unordered_map<KeyCode, bool> m_keyStates;
    };
    class CORE_API Timer {
    public:
        Timer() { Reset(); }

        void Reset() {
            m_startTime = std::chrono::high_resolution_clock::now();
        }

        float GetElapsedSeconds() const {
            auto currentTime = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> elapsed = currentTime - m_startTime;
            return elapsed.count();
        }

    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> m_startTime;
    };

}