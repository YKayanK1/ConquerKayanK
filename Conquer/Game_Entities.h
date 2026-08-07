// ============================================================================
// Conquer Kayank Engine
// ============================================================================
#pragma once
#include <cstdint>

namespace Game {
    enum class ModelType : int {
        SmallFemale = 1,
        BigFemale = 2,
        SmallMale = 3,
        BigMale = 4
    };
    enum class RoleActionType : int {
        StandBy = 100,
        Rest1 = 101,
        Rest2 = 102,
        Rest3 = 103,
        WalkL = 110,
        WalkR = 111,
        RunL = 120,
        RunR = 121,
        Jump = 130,
        JumpAtk = 140,
        Sit = 250,
        Alert = 300,
        HitPhysicalAttack = 320,
        Die = 330,
        DeadBody = 331,
        Attack0 = 350,
        Attack1 = 351,
        Attack2 = 352,
        PhysicalAttack_401 = 401,
        PhysicalAttack_402 = 402,
        PhysicalAttack_403 = 403,
        MagicalAttackCast = 903
    };

    struct PlayerEntity {
        float mapX = 0.0f, mapY = 0.0f;
        float targetMapX = 0.0f, targetMapY = 0.0f;
        bool isMoving = false, isJumping = false;
        float startMapX = 0.0f, startMapY = 0.0f;
        float jumpTimer = 0.0f, jumpZ = 0.0f;
        int currentFrame = 0;
        float animTimer = 0.0f;
        float facingAngle = -0.78539f;

        int nameTexId = -1, nameW = 0, nameH = 0;

        bool hasQueuedAction = false;
        bool queuedIsJump = false;
        float queuedTargetX = 0.0f, queuedTargetY = 0.0f;

        ModelType modelType = ModelType::SmallFemale;
        uint32_t armorId = 0;
        uint32_t rightHandWeaponId = 0;
        uint32_t leftHandWeaponId = 0;
        RoleActionType currentAction = RoleActionType::StandBy;

        // Sistema de batalha teste blablabla
        int targetMonsterIndex = -1;
        bool isAttacking = false;
        int currentAttackIndex = 0;

        // Quando o boneco ataca e fica em moto alerta.... a baixo tem o tempo pra ele ficar nessa animação, e o tempo que ele deve esperar para atacar novamente, e o alvo esta no alcance
        bool isAlert = false;
        float alertTimer = 0.0f;
        float attackCooldown = 0.0f;
        bool isChasing = false;
    };

    struct MonsterEntity {
        float mapX = 0.0f, mapY = 0.0f;
        float startX = 0.0f, startY = 0.0f;
        float targetX = 0.0f, targetY = 0.0f;
        bool isMoving = false;
        float waitTimer = 0.0f;
        float timeToWait = 2.0f;
        int currentFrame = 0;
        float animTimer = 0.0f;
        float facingAngle = -0.78539f;

        int hp = 100;
        int maxHp = 100;
        float visualHp = 100.0f;// Tentando deixar mais suave a barra do life, com a barra atras haha

        int nameTexId = -1, nameW = 0, nameH = 0;

		// Essa aqui era pra ser o dead, mas como o monstro morre e some, então é só pra saber se ele morreu mesmo, e não ficar atacando ele depois que ele morreu
        bool isDead = false;
        int currentAction = 100;
        float deathTimer = 0.0f;
        float alpha = 1.0f;
    };

    struct SceneObject {
        int textureId;
        float mapX, mapY;
        int width, height;
        int offsetX, offsetY;
    };
}