// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#include "pch.h"

#ifndef GAME_EXPORTS
#define GAME_EXPORTS
#endif

#ifdef GAME_EXPORTS
#define GAME_API __declspec(dllexport)
#else
#define GAME_API __declspec(dllimport)
#endif

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <cmath>
#include <iostream>
#include <typeindex>
#include <functional>

namespace Game {


    struct Vec2 { float x, y; };
    struct Vec3 { float x, y, z; };

    enum class EntityState {
        IDLE,
        WALKING,
        RUNNING,
        JUMPING,
        SITTING,
        ATTACKING,
        CASTING_MAGIC,
        DEAD
    };

    enum class EntityType {
        PLAYER,
        MONSTER,
        NPC,
        PET,
        MOUNT
    };

    enum class PKMode {
        PEACE,
        TEAM,
        CAPTURE,
        FREE,
        GHOST
    };


    class GameEntity;
    class GAME_API IComponent {
    public:
        virtual ~IComponent() = default;

        virtual void Update(float deltaTime) = 0;

        GameEntity* Owner = nullptr;
    };

    class GAME_API MovementComponent : public IComponent {
    public:
        Vec2 currentPosition = { 0.0f, 0.0f };
        Vec2 targetPosition = { 0.0f, 0.0f };
        float speed = 5.0f;
        bool isMoving = false;

        void MoveTo(Vec2 target) {
            targetPosition = target;
            isMoving = true;
        }

        void Update(float deltaTime) override {
            if (!isMoving) return;

            float dx = targetPosition.x - currentPosition.x;
            float dy = targetPosition.y - currentPosition.y;
            float distance = std::sqrt(dx * dx + dy * dy);

            if (distance < 0.1f) {
                currentPosition = targetPosition;
                isMoving = false;
            }
            else {
                currentPosition.x += (dx / distance) * speed * deltaTime;
                currentPosition.y += (dy / distance) * speed * deltaTime;
            }
        }
    };


    class GAME_API JumpComponent : public IComponent {
    public:
        bool isJumping = false;
        Vec2 startPos, endPos;
        float jumpTime = 0.0f;
        const float JUMP_DURATION = 0.5f;
        float currentHeight = 0.0f;

        void Jump(Vec2 target) {
            isJumping = true;
            jumpTime = 0.0f;
            endPos = target;
        }

        void Update(float deltaTime) override {
            if (!isJumping) return;

            jumpTime += deltaTime;
            float progress = jumpTime / JUMP_DURATION;

            if (progress >= 1.0f) {
                isJumping = false;
                currentHeight = 0.0f;
            }
            else {
                float maxHeight = 20.0f;
                currentHeight = -4.0f * maxHeight * (progress * progress) + 4.0f * maxHeight * progress;
            }
        }
    };


    class GAME_API EquipmentComponent : public IComponent {
    public:
        uint32_t headId = 0;
        uint32_t armorId = 0;
        uint32_t rightHandId = 0;
        uint32_t leftHandId = 0;
        uint32_t garmentId = 0;

        void EquipItem(int slot, uint32_t itemId) {
        }

        void Update(float deltaTime) override {  }
    };

    class GAME_API CombatComponent : public IComponent {
    public:
        int hp = 1000;
        int maxHp = 1000;
        int mana = 500;
        int stamina = 100;

        uint32_t targetId = 0;

        void TakeDamage(int amount) {
            hp -= amount;
            if (hp <= 0) {
                hp = 0;
            }
        }

        void Update(float deltaTime) override {
        }
    };

    class GAME_API AnimationComponent : public IComponent {
    public:
        uint32_t currentAction = 100; // 100 = Stand, 110 = Walk, etc.
        float animationSpeed = 1.0f;

        void PlayAction(uint32_t actionId) {
            currentAction = actionId;
        }

        void Update(float deltaTime) override {
        }
    };
    class GAME_API GameEntity {
    public:
        uint32_t UID;
        std::string Name;
        EntityType Type;
        EntityState State = EntityState::IDLE;
        template<typename T>
        void AddComponent(std::shared_ptr<T> component) {
            component->Owner = this;
            m_components[typeid(T)] = component;
        }

        template<typename T>
        std::shared_ptr<T> GetComponent() {
            auto it = m_components.find(typeid(T));
            if (it != m_components.end()) {
                return std::static_pointer_cast<T>(it->second);
            }
            return nullptr;
        }

        void Update(float deltaTime) {
            for (auto& pair : m_components) {
                pair.second->Update(deltaTime);
            }
        }

    private:
        std::unordered_map<std::type_index, std::shared_ptr<IComponent>> m_components;
    };


    class GAME_API Player : public GameEntity {
    public:
        Player(uint32_t uid, const std::string& name) {
            UID = uid;
            Name = name;
            Type = EntityType::PLAYER;
            PkMode = PKMode::PEACE;

            AddComponent(std::make_shared<MovementComponent>());
            AddComponent(std::make_shared<JumpComponent>());
            AddComponent(std::make_shared<CombatComponent>());
            AddComponent(std::make_shared<EquipmentComponent>());
            AddComponent(std::make_shared<AnimationComponent>());
        }

        PKMode PkMode;
        uint16_t Level;
        uint8_t Class; // Trojan, Warrior, Archer, etc.
        uint32_t GuildId;
    };

    class GAME_API Monster : public GameEntity {
    public:
        Monster(uint32_t uid, const std::string& name) {
            UID = uid;
            Name = name;
            Type = EntityType::MONSTER;

            AddComponent(std::make_shared<MovementComponent>());
            AddComponent(std::make_shared<CombatComponent>());
            AddComponent(std::make_shared<AnimationComponent>());
        }
    };

    class GAME_API CombatSystem {
    public:
        bool CanAttack(std::shared_ptr<Player> attacker, std::shared_ptr<GameEntity> target) {
            if (attacker->State == EntityState::DEAD || target->State == EntityState::DEAD) return false;

            if (target->Type == EntityType::PLAYER) {
                auto targetPlayer = std::static_pointer_cast<Player>(target);

                if (attacker->PkMode == PKMode::PEACE) return false;

                if (attacker->PkMode == PKMode::TEAM) {
                }

                if (attacker->PkMode == PKMode::CAPTURE) {
                }
            }

            return true;
        }

        void ProcessPhysicalAttack(std::shared_ptr<GameEntity> attacker, std::shared_ptr<GameEntity> target) {
            // 1. Calcula dano (Pega stats do EquipmentComponent)
            // 2. Aplica mitigação de armadura do target
            // 3. Modifica o HP via CombatComponent
            // 4. Dispara a animação (AnimationComponent) de ataque no attacker
            // 5. Dispara a animação de Hit (Dano) no target
        }
    };

    class GAME_API GameManager {
    public:
        static GameManager& GetInstance() {
            static GameManager instance;
            return instance;
        }

        void AddEntity(std::shared_ptr<GameEntity> entity) {
            m_entities[entity->UID] = entity;
        }

        void RemoveEntity(uint32_t uid) {
            m_entities.erase(uid);
        }

        void Update(float deltaTime) {
            for (auto& pair : m_entities) {
                pair.second->Update(deltaTime);
            }

        }

    private:
        GameManager() = default;
        std::unordered_map<uint32_t, std::shared_ptr<GameEntity>> m_entities;
    };

}