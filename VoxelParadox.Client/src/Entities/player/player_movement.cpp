// player.cpp
// Unity mental model: Player controller and simulation.
// Handles first-person camera look, grounded body locomotion,
// stance changes, block collisions, and hotbar selection.

#pragma region Includes

// 1. Standard Library
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

// 2. Local Project Modules
#include "player.hpp"
#include "audio/game_audio_controller.hpp"
#include "input/input_action_ids.hpp"
#include "input/input_action_system.hpp"

#pragma endregion

namespace {

#pragma region 1. Local Helpers & Constants
    // --- 1. Local Helpers & Constants ---

    constexpr float kCollisionEpsilon = 0.0005f;
    constexpr int kPenetrationResolveIterations = 8;
    constexpr float kMinecraftTicksPerSecond = 20.0f;
    constexpr float kMinecraftGroundDragPerTick = 0.546f;
    constexpr float kMinecraftAirDragPerTick = 0.91f;

    bool overlapsOnAxis(float minA, float maxA, float minB, float maxB) {
        return maxA > minB + kCollisionEpsilon && minA < maxB - kCollisionEpsilon;
    }

    glm::vec3 makeBodyMinCorner(const glm::vec3& feetPosition, float radius) {
        return feetPosition + glm::vec3(-radius, 0.0f, -radius);
    }

    glm::vec3 makeBodyMaxCorner(const glm::vec3& feetPosition, float radius, float bodyHeight) {
        return feetPosition + glm::vec3(radius, bodyHeight, radius);
    }

    float computeJumpVelocity(float gravityAcceleration, float jumpHeight) {
        return std::sqrt(glm::max(0.0f, 2.0f * gravityAcceleration * jumpHeight));
    }

    float horizontalLengthSquared(const glm::vec3& value) {
        return glm::dot(glm::vec2(value.x, value.z), glm::vec2(value.x, value.z));
    }

#pragma endregion

}  // namespace

#pragma region 2. Camera & View
// --- 2. Camera & View ---

void Player::updateCameraLook() {
    float mdx = 0.0f;
    float mdy = 0.0f;

    Input::getMouseDelta(mdx, mdy);
    camera.rotate(mdx, mdy);
}

void Player::updateZoom(float dt, bool allowMovementInput) {
    const bool zoomHeld =
        allowMovementInput &&
        InputMapping::InputActionSystem::instance().isDown(InputActionIds::kZoom);
    const float targetFov = zoomHeld ? glm::min(zoomedFov, normalFov) : normalFov;
    const float blend = std::min(1.0f, dt * zoomSmoothSpeed);

    camera.baseFov += (targetFov - camera.baseFov) * blend;
}

float Player::getCurrentBodyHeight() const {
    return crouching ? crouchingHeight : standingHeight;
}

float Player::getTargetEyeHeight() const {
    return crouching ? crouchingEyeHeight : standingEyeHeight;
}

glm::vec3 Player::getFeetPosition() const {
    return camera.position - glm::vec3(0.0f, currentEyeHeight, 0.0f);
}

void Player::setFeetPosition(const glm::vec3& feetPosition) {
    camera.position = feetPosition + glm::vec3(0.0f, currentEyeHeight, 0.0f);
}

#pragma endregion

#pragma region 3. Core State & Inventory
// --- 3. Core State & Inventory ---

void Player::clearTargetSelection() {
    targeting.clearTargetSelection();
}

void Player::clearTargetOnly() {
    targeting.clearTargetOnly();
}

void Player::resetBlockBreaking() {
    targeting.resetBlockBreaking();
}

void Player::notifyInventoryStateChanged() {
    if (audioController) {
        audioController->onInventoryStateChanged(isInventoryOpen());
    }
}

void Player::handleHotbarSelectionInput() {
    static constexpr const char* hotbarActionIds[PlayerHotbar::SLOT_COUNT] = {
        InputActionIds::kHotbarSlot1, InputActionIds::kHotbarSlot2,
        InputActionIds::kHotbarSlot3, InputActionIds::kHotbarSlot4,
        InputActionIds::kHotbarSlot5, InputActionIds::kHotbarSlot6,
        InputActionIds::kHotbarSlot7, InputActionIds::kHotbarSlot8,
        InputActionIds::kHotbarSlot9,
    };
    auto& inputActions = InputMapping::InputActionSystem::instance();

    // Direct slot selection
    for (int index = 0; index < PlayerHotbar::SLOT_COUNT; index++) {
        if (inputActions.wasPressed(hotbarActionIds[index])) {
            const int previousIndex = hotbar.getSelectedIndex();
            hotbar.selectSlot(index);

            if (audioController && hotbar.getSelectedIndex() != previousIndex) {
                audioController->onHotbarSelectionChanged();
            }
            break;
        }
    }

    // Scroll wheel selection
    const float scroll = Input::getScroll();
    if (std::abs(scroll) <= 0.001f) {
        return;
    }

    int steps = static_cast<int>(std::round(scroll));
    if (steps == 0) {
        steps = scroll > 0.0f ? 1 : -1;
    }

    int selected = hotbar.getSelectedIndex();
    const int direction = steps > 0 ? -1 : 1;

    for (int i = 0; i < std::abs(steps); i++) {
        selected = (selected + direction + PlayerHotbar::SLOT_COUNT) % PlayerHotbar::SLOT_COUNT;
    }

    const int previousIndex = hotbar.getSelectedIndex();
    hotbar.selectSlot(selected);

    if (audioController && hotbar.getSelectedIndex() != previousIndex) {
        audioController->onHotbarSelectionChanged();
    }
}

#pragma endregion

#pragma region 4. Input & Movement Simulation
// --- 4. Input & Movement Simulation ---

bool Player::wantsToCrouch(bool allowMovementInput) const {
    return allowMovementInput &&
        InputMapping::InputActionSystem::instance().isDown(InputActionIds::kCrouch);
}

void Player::updateStance(FractalWorld* world, bool allowMovementInput, float dt) {
    const glm::vec3 feetPosition = getFeetPosition();
    sneakKeyHeld = wantsToCrouch(allowMovementInput);

    if (sneakKeyHeld) {
        crouching = true;
    }
    else if (crouching && (!world || canOccupyBodyAt(world, feetPosition, standingHeight))) {
        crouching = false;
    }

    const float blend = glm::clamp(crouchTransitionSpeed * dt, 0.0f, 1.0f);
    currentEyeHeight = glm::mix(currentEyeHeight, getTargetEyeHeight(), blend);
    setFeetPosition(feetPosition);
}

void Player::updateMovementState() {
    const float horizontalSpeed = getHorizontalSpeed();

    if (!grounded) {
        movementState = velocity.y > kMovementStateSpeedThreshold
            ? PlayerMovementState::Jumping
            : PlayerMovementState::Falling;
        return;
    }

    if (crouching) {
        movementState = PlayerMovementState::Crouching;
        return;
    }

    if (horizontalSpeed <= kMovementStateSpeedThreshold) {
        movementState = PlayerMovementState::Idle;
        return;
    }

    const float runThreshold = glm::mix(walkSpeed, runSpeed, 0.65f);
    movementState = horizontalSpeed >= runThreshold
        ? PlayerMovementState::Running
        : PlayerMovementState::Walking;
}

void Player::handleMovement(float dt, bool allowMovementInput, bool& jumpPressConsumed) {
    auto& inputActions = InputMapping::InputActionSystem::instance();

    // Determine forward vector
    glm::vec3 forward = camera.getForward();
    forward.y = 0.0f;
    if (glm::dot(forward, forward) <= 0.0001f) {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    else {
        forward = glm::normalize(forward);
    }

    // Determine right vector
    glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
    if (glm::dot(right, right) <= 0.0001f) {
        right = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    else {
        right = glm::normalize(right);
    }

    // Process directional input
    glm::vec3 moveInput(0.0f);
    if (allowMovementInput) {
        if (inputActions.isDown(InputActionIds::kMoveForward)) moveInput += forward;
        if (inputActions.isDown(InputActionIds::kMoveBackward)) moveInput -= forward;
        if (inputActions.isDown(InputActionIds::kMoveRight)) moveInput += right;
        if (inputActions.isDown(InputActionIds::kMoveLeft)) moveInput -= right;
    }

    if (glm::dot(moveInput, moveInput) > 0.0001f) {
        moveInput = glm::normalize(moveInput);
    }

    // Calculate desired speed
    const bool running =
        allowMovementInput &&
        inputActions.isDown(InputActionIds::kRun) &&
        !crouching;

    const float targetSpeed = crouching ? crouchSpeed : (running ? runSpeed : walkSpeed);
    const glm::vec3 desiredHorizontalVelocity = moveInput * targetSpeed;

    // Apply Minecraft-like ground/air response
    glm::vec3 horizontalVelocity(velocity.x, 0.0f, velocity.z);
    const bool hasMoveInput = glm::dot(moveInput, moveInput) > 0.0001f;
    const float tickScale = glm::max(0.0f, dt * kMinecraftTicksPerSecond);
    const float groundAccelerationScale = groundAcceleration / kDefaultGroundAcceleration;
    const float airAccelerationScale = airAcceleration / kDefaultAirAcceleration;
    const float groundDecelerationScale = groundDeceleration / kDefaultGroundDeceleration;
    const float airDecelerationScale = airDeceleration / kDefaultAirDeceleration;
    const float groundFrictionScale = groundFriction / kDefaultGroundFriction;

    const float responseScale = grounded
        ? (hasMoveInput ? groundAccelerationScale : groundDecelerationScale)
        : (hasMoveInput ? airAccelerationScale : airDecelerationScale);
    const float frictionScale = grounded ? groundFrictionScale : 1.0f;
    const float baseDragPerTick = grounded
        ? kMinecraftGroundDragPerTick
        : kMinecraftAirDragPerTick;
    const float dragFactor = std::pow(baseDragPerTick, frictionScale * responseScale * tickScale);

    horizontalVelocity =
        desiredHorizontalVelocity +
        (horizontalVelocity - desiredHorizontalVelocity) * dragFactor;

    if (horizontalLengthSquared(horizontalVelocity) <= 0.000001f) {
        horizontalVelocity = glm::vec3(0.0f);
    }

    velocity.x = horizontalVelocity.x;
    velocity.z = horizontalVelocity.z;

    // Process jump
    if (grounded) {
        coyoteTimeRemainingSeconds = kCoyoteTimeSeconds;
    }
    else {
        coyoteTimeRemainingSeconds = glm::max(0.0f, coyoteTimeRemainingSeconds - dt);
    }

    const bool jumpPressedRaw =
        allowMovementInput && inputActions.wasPressed(InputActionIds::kJump);
    const bool jumpPressed = jumpPressedRaw && !jumpPressConsumed;
    if (jumpPressed) {
        jumpPressConsumed = true;
        jumpBufferRemainingSeconds = kJumpBufferSeconds;
        //std::printf("jumped!\n");
        if (audioController) {
            audioController->onPlayerJump();
        }
    }
    else {
        jumpBufferRemainingSeconds = glm::max(0.0f, jumpBufferRemainingSeconds - dt);
    }

    const double now = ENGINE::GETTIME();

    const bool hasBufferedJump = jumpPressed || jumpBufferRemainingSeconds > 0.0f;
    const bool groundJump = hasBufferedJump &&
        (grounded || coyoteTimeRemainingSeconds > 0.0f);
    // FEATURE: REPEATED AIR JUMPS ARE INTENTIONAL SO THE PLAYER CAN FLY. DO NOT TREAT THIS AS A BUG.
    const bool airJump = !grounded && jumpPressed;
    const bool doubleJumpReady =
        airJump && now <= doubleJumpWindowExpiresSeconds &&
        now >= nextDoubleJumpTimeSeconds;
    const bool shouldJumpNow = groundJump || airJump;

    if (shouldJumpNow) {
        velocity.y = computeJumpVelocity(gravityAcceleration, jumpHeight);
        if (groundJump) {
            doubleJumpWindowExpiresSeconds = now + kDoubleJumpWindowSeconds;
            coyoteTimeRemainingSeconds = 0.0f;
        }

        if (doubleJumpReady) {
            //std::cout << "[DEBUG] " << "Double jump Ready\n";
            if (audioController) {
                audioController->onPlayerDoubleJump();
            }
            velocity.y += computeJumpVelocity(gravityAcceleration, doubleJumpBoostHeight);
            nextDoubleJumpTimeSeconds = now + kDoubleJumpCooldownSeconds;
            doubleJumpWindowExpiresSeconds = 0.0;
        }

        jumpBufferRemainingSeconds = 0.0f;
        grounded = false;
    }
    else if (grounded) {
        velocity.y = -gravityAcceleration * dt;
    }
    else {
        velocity.y -= gravityAcceleration * dt;
    }
}

void Player::simulateMovement(float dt, FractalWorld* world, bool allowMovementInput) {
    // Split physics into substeps to keep the body stable against the voxel grid.
    float remaining = dt;
    bool jumpPressConsumed = false;

    while (remaining > 0.0f) {
        const float step = std::min(remaining, kPhysicsMaxStep);

        if (world) {
            glm::vec3 feetPosition = getFeetPosition();
            resolveBodyPenetration(
                world,
                feetPosition,
                getCurrentBodyHeight(),
                false
            );
            setFeetPosition(feetPosition);
        }

        updateStance(world, allowMovementInput, step);

        grounded =
            world &&
            hasGroundSupport(world, getFeetPosition()) &&
            velocity.y <= 0.0f;

        handleMovement(step, allowMovementInput, jumpPressConsumed);

        if (world) {
            resolveCollisions(world, step);
        }
        else {
            camera.position += velocity * step;
            grounded = false;
        }

        remaining -= step;
    }

    updateMovementState();
}

#pragma endregion

#pragma region 5. Physics & Collisions
// --- 5. Physics & Collisions ---

bool Player::canOccupyBodyAt(FractalWorld* world, const glm::vec3& feetPosition, float bodyHeight) const {
    if (!world) {
        return true;
    }

    const glm::vec3 minCorner = makeBodyMinCorner(feetPosition, playerRadius);
    const glm::vec3 maxCorner = makeBodyMaxCorner(feetPosition, playerRadius, bodyHeight) - glm::vec3(kCollisionEpsilon);

    const glm::ivec3 minBlock = glm::ivec3(glm::floor(minCorner));
    const glm::ivec3 maxBlock = glm::ivec3(glm::floor(maxCorner));

    for (int bx = minBlock.x; bx <= maxBlock.x; ++bx) {
        for (int by = minBlock.y; by <= maxBlock.y; ++by) {
            for (int bz = minBlock.z; bz <= maxBlock.z; ++bz) {
                if (isSolid(world->getBlock(glm::ivec3(bx, by, bz)))) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool Player::doesBlockOverlapBody(const glm::ivec3& blockPos,
                                  const glm::vec3& feetPosition,
                                  float bodyHeight) const {
    const glm::vec3 minCorner = makeBodyMinCorner(feetPosition, playerRadius);
    const glm::vec3 maxCorner =
        makeBodyMaxCorner(feetPosition, playerRadius, bodyHeight) -
        glm::vec3(kCollisionEpsilon);

    const glm::vec3 blockMin = glm::vec3(blockPos);
    const glm::vec3 blockMax = blockMin + glm::vec3(1.0f);

    return overlapsOnAxis(minCorner.x, maxCorner.x, blockMin.x, blockMax.x) &&
        overlapsOnAxis(minCorner.y, maxCorner.y, blockMin.y, blockMax.y) &&
        overlapsOnAxis(minCorner.z, maxCorner.z, blockMin.z, blockMax.z);
}

bool Player::doesBlockOverlapCurrentBody(const glm::ivec3& blockPos) const {
    return doesBlockOverlapBody(blockPos, getFeetPosition(), getCurrentBodyHeight());
}

std::pair<float, bool> Player::sweepBodyAxis(FractalWorld* world,
                                             const glm::vec3& feetPosition,
                                             float bodyHeight,
                                             int axis,
                                             float delta) const {
    if (!world || std::abs(delta) <= kCollisionEpsilon) {
        return std::pair<float, bool>(feetPosition[axis], false);
    }

    glm::vec3 candidateFeet = feetPosition;
    candidateFeet[axis] += delta;

    const glm::vec3 minCorner = makeBodyMinCorner(candidateFeet, playerRadius);
    const glm::vec3 maxCorner =
        makeBodyMaxCorner(candidateFeet, playerRadius, bodyHeight) -
        glm::vec3(kCollisionEpsilon);

    const glm::ivec3 minBlock = glm::ivec3(glm::floor(minCorner));
    const glm::ivec3 maxBlock = glm::ivec3(glm::floor(maxCorner));

    float resolvedAxis = candidateFeet[axis];

    for (int bx = minBlock.x; bx <= maxBlock.x; ++bx) {
        for (int by = minBlock.y; by <= maxBlock.y; ++by) {
            for (int bz = minBlock.z; bz <= maxBlock.z; ++bz) {

                const glm::ivec3 blockPos(bx, by, bz);
                if (!isSolid(world->getBlock(blockPos))) {
                    continue;
                }

                const glm::vec3 blockMin = glm::vec3(blockPos);
                const glm::vec3 blockMax = blockMin + glm::vec3(1.0f);

                if (axis == 0) { // X Axis
                    if (!overlapsOnAxis(minCorner.y, maxCorner.y, blockMin.y, blockMax.y) ||
                        !overlapsOnAxis(minCorner.z, maxCorner.z, blockMin.z, blockMax.z)) {
                        continue;
                    }
                    if (delta > 0.0f) {
                        resolvedAxis = std::min(resolvedAxis, blockMin.x - playerRadius - kCollisionEpsilon);
                    }
                    else {
                        resolvedAxis = std::max(resolvedAxis, blockMax.x + playerRadius + kCollisionEpsilon);
                    }
                }
                else if (axis == 1) { // Y Axis
                    if (!overlapsOnAxis(minCorner.x, maxCorner.x, blockMin.x, blockMax.x) ||
                        !overlapsOnAxis(minCorner.z, maxCorner.z, blockMin.z, blockMax.z)) {
                        continue;
                    }
                    if (delta > 0.0f) {
                        resolvedAxis = std::min(resolvedAxis, blockMin.y - bodyHeight - kCollisionEpsilon);
                    }
                    else {
                        resolvedAxis = std::max(resolvedAxis, blockMax.y + kCollisionEpsilon);
                    }
                }
                else { // Z Axis
                    if (!overlapsOnAxis(minCorner.x, maxCorner.x, blockMin.x, blockMax.x) ||
                        !overlapsOnAxis(minCorner.y, maxCorner.y, blockMin.y, blockMax.y)) {
                        continue;
                    }
                    if (delta > 0.0f) {
                        resolvedAxis = std::min(resolvedAxis, blockMin.z - playerRadius - kCollisionEpsilon);
                    }
                    else {
                        resolvedAxis = std::max(resolvedAxis, blockMax.z + playerRadius + kCollisionEpsilon);
                    }
                }
            }
        }
    }

    const bool collided = std::abs(resolvedAxis - candidateFeet[axis]) > kCollisionEpsilon;
    return std::pair<float, bool>(resolvedAxis, collided);
}

bool Player::hasSupportBelow(FractalWorld* world,
                             const glm::vec3& feetPosition,
                             float inset,
                             bool requireAllSamples) const {
    if (!world) {
        return false;
    }

    const float sampleRadius = glm::max(0.0f, playerRadius - inset);
    const std::array<glm::vec2, 5> samples = {{
        glm::vec2(0.0f, 0.0f),
        glm::vec2(-sampleRadius, -sampleRadius),
        glm::vec2(-sampleRadius, sampleRadius),
        glm::vec2(sampleRadius, -sampleRadius),
        glm::vec2(sampleRadius, sampleRadius),
    }};

    const int supportY =
        static_cast<int>(std::floor(feetPosition.y - kGroundProbeDistance));
    bool anySupported = false;

    for (const glm::vec2& sampleOffset : samples) {
        const glm::ivec3 supportBlock(
            static_cast<int>(std::floor(feetPosition.x + sampleOffset.x)),
            supportY,
            static_cast<int>(std::floor(feetPosition.z + sampleOffset.y))
        );

        const bool supported = isSolid(world->getBlock(supportBlock));

        if (requireAllSamples && !supported) {
            return false;
        }

        anySupported = anySupported || supported;
    }

    return requireAllSamples ? true : anySupported;
}

bool Player::hasGroundSupport(FractalWorld* world, const glm::vec3& feetPosition) const {
    if (!world) {
        return false;
    }

    const float minimumSupportX = feetPosition.x - playerRadius;
    const float maximumSupportX = feetPosition.x + playerRadius;
    const float minimumSupportZ = feetPosition.z - playerRadius;
    const float maximumSupportZ = feetPosition.z + playerRadius;

    const int supportY =
        static_cast<int>(std::floor(feetPosition.y - kGroundProbeDistance));
    const int minimumBlockX =
        static_cast<int>(std::floor(minimumSupportX));
    const int maximumBlockX =
        static_cast<int>(std::floor(maximumSupportX));
    const int minimumBlockZ =
        static_cast<int>(std::floor(minimumSupportZ));
    const int maximumBlockZ =
        static_cast<int>(std::floor(maximumSupportZ));

    for (int blockX = minimumBlockX; blockX <= maximumBlockX; ++blockX) {
        for (int blockZ = minimumBlockZ; blockZ <= maximumBlockZ; ++blockZ) {
            const glm::ivec3 blockPos(blockX, supportY, blockZ);
            if (!isSolid(world->getBlock(blockPos))) {
                continue;
            }

            const glm::vec3 blockMin = glm::vec3(blockPos);
            const glm::vec3 blockMax = blockMin + glm::vec3(1.0f);
            const float supportDistance = feetPosition.y - blockMax.y;

            if (supportDistance < -kCollisionEpsilon ||
                supportDistance > kGroundProbeDistance + kCollisionEpsilon) {
                continue;
            }

            if (!overlapsOnAxis(minimumSupportX, maximumSupportX, blockMin.x, blockMax.x) ||
                !overlapsOnAxis(minimumSupportZ, maximumSupportZ, blockMin.z, blockMax.z)) {
                continue;
            }

            return true;
        }
    }

    return false;
}

bool Player::snapToGround(FractalWorld* world,
                          glm::vec3& feetPosition,
                          float bodyHeight) const {
    if (!world) {
        return false;
    }

    const auto [snappedY, collidedY] =
        sweepBodyAxis(world, feetPosition, bodyHeight, 1, -kGroundSnapDistance);
    if (!collidedY || snappedY > feetPosition.y + kCollisionEpsilon) {
        return false;
    }

    glm::vec3 snappedFeet = feetPosition;
    snappedFeet.y = snappedY;

    if (!canOccupyBodyAt(world, snappedFeet, bodyHeight) ||
        !hasGroundSupport(world, snappedFeet)) {
        return false;
    }

    feetPosition = snappedFeet;
    return true;
}

bool Player::tryStepUp(FractalWorld* world,
                       const glm::vec3& startFeetPosition,
                       float bodyHeight,
                       const glm::vec3& desiredHorizontalDelta,
                       glm::vec3& outFeetPosition) const {
    if (!world || horizontalLengthSquared(desiredHorizontalDelta) <= 0.0001f) {
        return false;
    }

    if (!hasGroundSupport(world, startFeetPosition)) {
        return false;
    }

    glm::vec3 raisedFeet = startFeetPosition;
    raisedFeet.y += kMaxStepHeight;
    if (!canOccupyBodyAt(world, raisedFeet, bodyHeight)) {
        return false;
    }

    glm::vec3 steppedFeet = raisedFeet;

    const std::pair<float, bool> xSweep =
        sweepBodyAxis(world, steppedFeet, bodyHeight, 0, desiredHorizontalDelta.x);
    steppedFeet.x = xSweep.first;

    const std::pair<float, bool> zSweep =
        sweepBodyAxis(world, steppedFeet, bodyHeight, 2, desiredHorizontalDelta.z);
    steppedFeet.z = zSweep.first;

    const glm::vec3 steppedDelta = steppedFeet - raisedFeet;
    if (horizontalLengthSquared(steppedDelta) <= 0.0001f) {
        return false;
    }

    const auto [resolvedY, collidedY] =
        sweepBodyAxis(world, steppedFeet, bodyHeight, 1, -(kMaxStepHeight + kGroundSnapDistance));
    if (!collidedY) {
        return false;
    }

    steppedFeet.y = resolvedY;
    if (steppedFeet.y <= startFeetPosition.y + kCollisionEpsilon) {
        return false;
    }

    if (!canOccupyBodyAt(world, steppedFeet, bodyHeight) ||
        !hasGroundSupport(world, steppedFeet)) {
        return false;
    }

    outFeetPosition = steppedFeet;
    return true;
}

void Player::updateEmbeddedHeadDamage(FractalWorld* world, float dt) {
    if (!world || !isAlive()) {
        headEmbeddedDamageTimer = kHeadEmbeddedDamageIntervalSeconds;
        return;
    }

    const glm::ivec3 headBlock =
        glm::ivec3(glm::floor(camera.position));
    if (!isSolid(world->getBlock(headBlock))) {
        headEmbeddedDamageTimer = kHeadEmbeddedDamageIntervalSeconds;
        return;
    }

    headEmbeddedDamageTimer -= dt;
    if (headEmbeddedDamageTimer > 0.0f) {
        return;
    }

    applyDamage(kHeadEmbeddedDamagePoints);
    headEmbeddedDamageTimer = kHeadEmbeddedDamageIntervalSeconds;
}

void Player::resolveBodyPenetration(FractalWorld* world,
                                    glm::vec3& feetPosition,
                                    float bodyHeight,
                                    bool allowUpwardCorrection) {
    if (!world) {
        return;
    }

    for (int iteration = 0; iteration < kPenetrationResolveIterations; ++iteration) {
        const glm::vec3 minCorner = makeBodyMinCorner(feetPosition, playerRadius);
        const glm::vec3 maxCorner = makeBodyMaxCorner(feetPosition, playerRadius, bodyHeight) - glm::vec3(kCollisionEpsilon);

        const glm::ivec3 minBlock = glm::ivec3(glm::floor(minCorner));
        const glm::ivec3 maxBlock = glm::ivec3(glm::floor(maxCorner));

        bool resolvedOverlap = false;

        for (int bx = minBlock.x; bx <= maxBlock.x && !resolvedOverlap; ++bx) {
            for (int by = minBlock.y; by <= maxBlock.y && !resolvedOverlap; ++by) {
                for (int bz = minBlock.z; bz <= maxBlock.z; ++bz) {

                    const glm::ivec3 blockPos(bx, by, bz);
                    if (!isSolid(world->getBlock(blockPos))) {
                        continue;
                    }

                    const glm::vec3 blockMin = glm::vec3(blockPos);
                    const glm::vec3 blockMax = blockMin + glm::vec3(1.0f);

                    if (!overlapsOnAxis(minCorner.x, maxCorner.x, blockMin.x, blockMax.x) ||
                        !overlapsOnAxis(minCorner.y, maxCorner.y, blockMin.y, blockMax.y) ||
                        !overlapsOnAxis(minCorner.z, maxCorner.z, blockMin.z, blockMax.z)) {
                        continue;
                    }

                    const float pushPositiveX = blockMax.x - minCorner.x;
                    const float pushNegativeX = maxCorner.x - blockMin.x;
                    const float pushPositiveY = blockMax.y - minCorner.y;
                    const float pushNegativeY = maxCorner.y - blockMin.y;
                    const float pushPositiveZ = blockMax.z - minCorner.z;
                    const float pushNegativeZ = maxCorner.z - blockMin.z;

                    struct PushCandidate {
                        glm::vec3 vector{ 0.0f };
                        float distance = 0.0f;
                        bool valid = false;
                    };

                    const bool blockIsBelowFeet =
                        blockMax.y <= feetPosition.y + kCollisionEpsilon;

                    const std::array<PushCandidate, 6> candidates = {{
                        { glm::vec3(pushPositiveX + kCollisionEpsilon, 0.0f, 0.0f), pushPositiveX, true },
                        { glm::vec3(-(pushNegativeX + kCollisionEpsilon), 0.0f, 0.0f), pushNegativeX, true },
                        { glm::vec3(0.0f, pushPositiveY + kCollisionEpsilon, 0.0f), pushPositiveY, allowUpwardCorrection && blockIsBelowFeet },
                        { glm::vec3(0.0f, -(pushNegativeY + kCollisionEpsilon), 0.0f), pushNegativeY, true },
                        { glm::vec3(0.0f, 0.0f, pushPositiveZ + kCollisionEpsilon), pushPositiveZ, true },
                        { glm::vec3(0.0f, 0.0f, -(pushNegativeZ + kCollisionEpsilon)), pushNegativeZ, true },
                    }};

                    PushCandidate bestCandidate{};
                    bool hasCandidate = false;

                    for (const PushCandidate& candidate : candidates) {
                        if (!candidate.valid) {
                            continue;
                        }

                        if (!hasCandidate || candidate.distance < bestCandidate.distance) {
                            bestCandidate = candidate;
                            hasCandidate = true;
                        }
                    }

                    if (!hasCandidate) {
                        continue;
                    }

                    const glm::vec3 pushVector = bestCandidate.vector;
                    feetPosition += pushVector;

                    if (pushVector.y > 0.0f && velocity.y < 0.0f) {
                        grounded = true;
                        velocity.y = 0.0f;
                    }
                    if (pushVector.y < 0.0f && velocity.y > 0.0f) {
                        velocity.y = 0.0f;
                    }

                    resolvedOverlap = true;
                    break;
                }
            }
        }

        if (!resolvedOverlap) {
            return;
        }
    }
}

void Player::resolveCollisions(FractalWorld* world, float dt) {
    if (!world) {
        camera.position += velocity * dt;
        grounded = false;
        return;
    }

    glm::vec3 feetPosition = getFeetPosition();
    const float bodyHeight = getCurrentBodyHeight();
    const bool startedGrounded = grounded;
    const glm::vec3 startFeetPosition = feetPosition;
    glm::vec3 desiredHorizontalDelta(velocity.x * dt, 0.0f, velocity.z * dt);
    const bool applySneakEdgeProtection =
        crouching &&
        startedGrounded &&
        velocity.y <= 0.0f;

    if (applySneakEdgeProtection) {
        const glm::vec3 candidateFeet =
            startFeetPosition + glm::vec3(desiredHorizontalDelta.x, 0.0f, 0.0f);
        if (!hasGroundSupport(world, candidateFeet)) {
            desiredHorizontalDelta.x = 0.0f;
        }
    }

    const auto [resolvedX, collidedX] =
        sweepBodyAxis(world, feetPosition, bodyHeight, 0, desiredHorizontalDelta.x);
    feetPosition.x = resolvedX;

    if (applySneakEdgeProtection) {
        const glm::vec3 candidateFeet =
            feetPosition + glm::vec3(0.0f, 0.0f, desiredHorizontalDelta.z);
        if (!hasGroundSupport(world, candidateFeet)) {
            desiredHorizontalDelta.z = 0.0f;
        }
    }

    const auto [resolvedZ, collidedZ] =
        sweepBodyAxis(world, feetPosition, bodyHeight, 2, desiredHorizontalDelta.z);
    feetPosition.z = resolvedZ;

    if (startedGrounded && velocity.y <= 0.0f && (collidedX || collidedZ)) {
        glm::vec3 steppedFeet(0.0f);
        if (tryStepUp(world, startFeetPosition, bodyHeight, desiredHorizontalDelta, steppedFeet)) {
            feetPosition = steppedFeet;
        }
    }

    if (std::abs((feetPosition.x - startFeetPosition.x) - desiredHorizontalDelta.x) > kCollisionEpsilon) {
        velocity.x = 0.0f;
    }
    if (std::abs((feetPosition.z - startFeetPosition.z) - desiredHorizontalDelta.z) > kCollisionEpsilon) {
        velocity.z = 0.0f;
    }

    // Sweep Y
    grounded = false;
    const auto [resolvedY, collidedY] =
        sweepBodyAxis(world, feetPosition, bodyHeight, 1, velocity.y * dt);

    if (collidedY) {
        if (velocity.y < 0.0f) {
            grounded = true;
        }
        velocity.y = 0.0f;
    }
    feetPosition.y = resolvedY;

    // Final checks
    resolveBodyPenetration(world, feetPosition, bodyHeight, false);

    if (!grounded && velocity.y <= 0.0f && snapToGround(world, feetPosition, bodyHeight)) {
        grounded = true;
    }
    if (!grounded && velocity.y <= 0.0f) {
        grounded = hasGroundSupport(world, feetPosition);
    }
    if (grounded && velocity.y < 0.0f) {
        velocity.y = 0.0f;
    }

    setFeetPosition(feetPosition);
}

#pragma endregion
