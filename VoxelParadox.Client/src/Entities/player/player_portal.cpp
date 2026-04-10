#include "player.hpp"

#include "audio/game_audio_controller.hpp"
#include "engine/engine.hpp"

// Portal and nested-world logic:
// - portal math
// - preview anchor state
// - transition playback
// - enter/exit traversal

float Player::smoothstep01(float t) {
    const float clamped = glm::clamp(t, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

void Player::buildPortalBasis(glm::ivec3 faceNormal, glm::vec3& normal,
                              glm::vec3& tangent, glm::vec3& bitangent) {
    normal = glm::normalize(glm::vec3(faceNormal));
    const glm::vec3 helper = std::abs(normal.y) > 0.99f
        ? glm::vec3(1.0f, 0.0f, 0.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);
    tangent = glm::normalize(glm::cross(helper, normal));
    bitangent = glm::normalize(glm::cross(normal, tangent));
}

Player::NestedPreviewFrame Player::buildNestedPreviewFrame(
    const NestedPreviewPortal& portal) {
    glm::vec3 normal(0.0f), tangent(0.0f), bitangent(0.0f);
    buildPortalBasis(portal.normal, normal, tangent, bitangent);

    NestedPreviewFrame frame;
    frame.center = glm::vec3(portal.block) + glm::vec3(0.5f) + normal * (0.5f + 0.01f);
    frame.right = tangent;
    frame.up = bitangent;
    frame.front = normal;
    return frame;
}

Player::NestedPreviewFrame Player::defaultNestedPreviewFrame() {
    NestedPreviewFrame frame;
    frame.center = nestedWorldSpawnPosition();
    frame.right = glm::vec3(1.0f, 0.0f, 0.0f);
    frame.up = glm::vec3(0.0f, 1.0f, 0.0f);
    frame.front = glm::vec3(0.0f, 0.0f, 1.0f);
    return frame;
}

glm::vec3 Player::toPortalLocal(const NestedPreviewFrame& frame,
                                const glm::vec3& worldVec) {
    return glm::vec3(glm::dot(worldVec, frame.right),
                     glm::dot(worldVec, frame.up),
                     glm::dot(worldVec, frame.front));
}

glm::vec3 Player::fromPortalLocal(const NestedPreviewFrame& frame,
                                  const glm::vec3& localVec) {
    return frame.right * localVec.x +
           frame.up * localVec.y +
           frame.front * localVec.z;
}

Player::NestedPreviewFrame Player::buildPreviewOverrideFrame(
    const Camera& source, const NestedPreviewPortal& portal,
    const Camera& desiredNestedCamera) {
    // When the child world already has a saved camera, align the preview to it.
    const NestedPreviewFrame entryFrame = buildNestedPreviewFrame(portal);
    const glm::vec3 relPos = source.position - entryFrame.center;
    const glm::vec3 localPos = toPortalLocal(entryFrame, relPos);
    const glm::vec3 localForward = toPortalLocal(entryFrame, source.getForward());
    const glm::vec3 localUp = toPortalLocal(entryFrame, source.getUp());

    const glm::vec3 mappedPos(-localPos.x, localPos.y, -localPos.z);
    const glm::vec3 mappedForward =
        glm::normalize(glm::vec3(-localForward.x, localForward.y, -localForward.z));
    glm::vec3 mappedUp =
        glm::normalize(glm::vec3(-localUp.x, localUp.y, -localUp.z));

    glm::vec3 localRight = glm::cross(mappedForward, mappedUp);
    if (glm::dot(localRight, localRight) < 1e-6f) {
        localRight = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        localRight = glm::normalize(localRight);
    }
    mappedUp = glm::normalize(glm::cross(localRight, mappedForward));

    const glm::vec3 desiredForward = glm::normalize(desiredNestedCamera.getForward());
    const glm::vec3 desiredUp = glm::normalize(desiredNestedCamera.getUp());
    const glm::vec3 desiredRight = glm::normalize(desiredNestedCamera.getRight());

    const glm::mat3 localBasis(localRight, mappedUp, mappedForward);
    const glm::mat3 desiredBasis(desiredRight, desiredUp, desiredForward);
    const glm::mat3 rotation = desiredBasis * glm::transpose(localBasis);

    NestedPreviewFrame frame;
    frame.right = glm::normalize(rotation[0]);
    frame.up = glm::normalize(rotation[1]);
    frame.front = glm::normalize(rotation[2]);
    frame.center = desiredNestedCamera.position - fromPortalLocal(frame, mappedPos);
    return frame;
}

void Player::clearNestedPreview() {
    nestedPreview.active = false;
    nestedPreview.block = glm::ivec3(0);
    nestedPreview.normal = glm::ivec3(0);
    nestedPreview.fade = 0.0f;
    nestedPreview.hasOverrideFrame = false;
    nestedPreview.overrideFrame = NestedPreviewFrame{};
}

void Player::activateNestedPreview(glm::ivec3 block, glm::ivec3 normal, float fadeValue) {
    const bool changedPortal =
        nestedPreview.block != block || nestedPreview.normal != normal;
    if (changedPortal) {
        nestedPreview.hasOverrideFrame = false;
        nestedPreview.overrideFrame = NestedPreviewFrame{};
    }
    nestedPreview.active = true;
    nestedPreview.block = block;
    nestedPreview.normal = normal;
    nestedPreview.fade = glm::clamp(fadeValue, 0.0f, 1.0f);
}

void Player::beginNestedPreviewFadeIn(glm::ivec3 block, glm::ivec3 normal) {
    activateNestedPreview(block, normal, nestedPreview.fade);
}

void Player::showNestedPreviewImmediately(glm::ivec3 block, glm::ivec3 normal) {
    activateNestedPreview(block, normal, 1.0f);
}

void Player::updateNestedPreviewAnchorFromSavedState(WorldStack& worldStack,
                                                     glm::ivec3 block,
                                                     glm::ivec3 normal) {
    glm::vec3 savedChildPos(0.0f);
    glm::quat savedChildOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    if (!worldStack.tryGetNestedPlayerState(block, savedChildPos, savedChildOrientation)) {
        nestedPreview.hasOverrideFrame = false;
        nestedPreview.overrideFrame = NestedPreviewFrame{};
        return;
    }

    NestedPreviewPortal previewPortal = nestedPreview;
    previewPortal.block = block;
    previewPortal.normal = normal;
    previewPortal.hasOverrideFrame = false;
    previewPortal.overrideFrame = NestedPreviewFrame{};

    Camera savedChildCamera = camera;
    savedChildCamera.position = savedChildPos;
    savedChildCamera.orientation = savedChildOrientation;
    enforceSafeNestedSpawn(worldStack, block, savedChildCamera, false);
    setNestedPreviewOverrideFrame(
        buildPreviewOverrideFrame(camera, previewPortal, savedChildCamera));
}

void Player::setNestedPreviewOverrideFrame(const NestedPreviewFrame& frame) {
    nestedPreview.hasOverrideFrame = true;
    nestedPreview.overrideFrame = frame;
}

bool Player::isLookingAtPortal(FractalWorld* world) const {
    return world && hasTarget && world->getBlock(targetBlock) == BlockIds::PORTAL;
}

bool Player::tryCreatePortalForTargetBlock(WorldStack& worldStack) {
    if (!hasTarget) {
        return false;
    }

    return tryPrepareNestedWorld(worldStack, targetBlock);
}

bool Player::tryPrepareNestedWorld(
    WorldStack& worldStack, const glm::ivec3& blockPos, std::uint32_t* outChildSeed,
    BiomeSelection* outChildBiome,
    std::shared_ptr<const VoxelGame::BiomePreset>* outChildPreset) {
    if (!worldStack.ensureNestedWorldAtBlock(blockPos, outChildSeed, outChildBiome,
                                            outChildPreset)) {
        return false;
    }

    return true;
}

void Player::handleTransition(float dt, WorldStack& worldStack) {
    transitionTimer += dt;
    const float t = glm::clamp(transitionTimer / transitionDuration, 0.0f, 1.0f);
    const float smoothT = smoothstep01(t);

    if (transition == PlayerTransition::DIVING_IN) {
        nestedPreview.active = true;
        nestedPreview.fade = glm::clamp(
            nestedPreview.fade + dt / previewFadeInDuration, 0.0f, 1.0f);

        camera.position = glm::mix(enterNested.startPos, enterNested.targetPos, smoothT);
        camera.orientation = glm::normalize(
            glm::slerp(enterNested.startOrientation, enterNested.targetOrientation, smoothT));

        if (t >= 1.0f) {
            finishDiveIn(worldStack);
        }
        return;
    }

    if (transition == PlayerTransition::RISING_OUT) {
        camera.position = glm::mix(transitionStartPos, transitionEndPos, smoothT);
        camera.orientation = glm::normalize(
            glm::slerp(transitionStartOrientation, transitionEndOrientation, smoothT));
        nestedPreview.active = true;
        nestedPreview.fade = 1.0f;
        if (t >= 1.0f) {
            transition = PlayerTransition::NONE;
        }
    }
}

void Player::finishDiveIn(WorldStack& worldStack) {
    if (worldStack.descendInto(enterNested.block, enterNested.parentReturnPos,
                               enterNested.parentReturnOrientation, enterNested.normal)) {
        camera.position = enterNested.childPos;
        camera.orientation = enterNested.childOrientation;
    }

    velocity = glm::vec3(0.0f);
    clearNestedPreview();
    transition = PlayerTransition::NONE;
}

void Player::updateNestedPreview(WorldStack& worldStack, FractalWorld* world, float dt) {
    if (!world) {
        clearNestedPreview();
        return;
    }

    const bool lookingAtPortal = isLookingAtPortal(world);
    updatePreviewVisibility(worldStack, lookingAtPortal, dt);
    preloadNearbyNestedWorld(worldStack, world, lookingAtPortal);
}

void Player::updatePreviewVisibility(WorldStack& worldStack, bool lookingAtPortal, float dt) {
    if (lookingAtPortal && tryPrepareNestedWorld(worldStack, targetBlock)) {
        const bool previewReloaded = !nestedPreview.active || nestedPreview.fade <= 0.0f;
        const bool portalChanged =
            nestedPreview.block != targetBlock || nestedPreview.normal != targetNormal;
        const bool needsSavedStateAnchor =
            portalChanged || (!nestedPreview.hasOverrideFrame && previewReloaded);
        activateNestedPreview(
            targetBlock,
            targetNormal,
            nestedPreview.fade + dt / previewFadeInDuration);
        if (needsSavedStateAnchor) {
            updateNestedPreviewAnchorFromSavedState(worldStack, targetBlock, targetNormal);
        }
        return;
    }

    if (nestedPreview.fade <= 0.0f) {
        nestedPreview.active = false;
        return;
    }

    nestedPreview.fade = glm::clamp(
        nestedPreview.fade - dt / previewFadeOutDuration, 0.0f, 1.0f);
    if (nestedPreview.fade <= 0.0f) {
        nestedPreview.active = false;
    }
}

void Player::preloadNearbyNestedWorld(WorldStack& worldStack, FractalWorld* world,
                                      bool lookingAtPortal) {
    (void)world;
    if (!lookingAtPortal) {
        if (!nestedPreview.active) {
            worldStack.clearNestedPreviewWorld();
        }
        return;
    }

    if (!worldStack.hasNestedWorldAtBlock(targetBlock) &&
        !tryPrepareNestedWorld(worldStack, targetBlock)) {
        worldStack.clearNestedPreviewWorld();
        return;
    }

    FractalWorld* previewWorld = worldStack.getOrCreateNestedPreviewWorld(targetBlock);
    if (!previewWorld) {
        return;
    }

    previewWorld->renderDistance =
        std::min(previewWorld->defaultRenderDistance, previewPreloadRenderDistance);
    previewWorld->update(nestedWorldSpawnPosition(), camera.getForward());
}

void Player::enforceSafeNestedSpawn(WorldStack& worldStack, const glm::ivec3& blockPos,
                                    Camera& nestedCamera, bool requireSupportBelow) {
    FractalWorld* nestedWorld = worldStack.getOrCreateNestedPreviewWorld(blockPos);
    if (!nestedWorld) {
        nestedCamera.position = nestedWorldSpawnPosition();
        return;
    }

    const glm::vec3 candidateFeet =
        nestedCamera.position - glm::vec3(0.0f, standingEyeHeight, 0.0f);
    const bool hasRoom = canOccupyBodyAt(nestedWorld, candidateFeet, standingHeight);
    const bool hasGround =
        !requireSupportBelow || hasSupportBelow(nestedWorld, candidateFeet, 0.0f, false);
    if (hasRoom && hasGround) {
        return;
    }

    nestedCamera.position = nestedWorldSpawnPosition();
}

void Player::beginAscendTransition(WorldStack& worldStack) {
    if (!worldStack.canAscend()) {
        return;
    }

    const Camera childCamera = camera;
    worldStack.saveActivePlayerState(childCamera.position, childCamera.orientation);

    glm::vec3 returnPos(0.0f);
    glm::quat returnOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    glm::ivec3 portalBlock(0);
    glm::ivec3 portalNormal(0);

    if (!worldStack.ascend(returnPos, returnOrientation, portalBlock, portalNormal)) {
        transition = PlayerTransition::NONE;
        return;
    }

    showNestedPreviewImmediately(portalBlock, portalNormal);
    if (audioController) {
        audioController->onPortalExited(portalBlock);
    }

    glm::vec3 normal(0.0f), tangent(0.0f), bitangent(0.0f);
    buildPortalBasis(portalNormal, normal, tangent, bitangent);

    const glm::vec3 faceCenter =
        glm::vec3(portalBlock) + glm::vec3(0.5f) + normal * 0.5f;
    const glm::vec3 safeCameraPos = faceCenter + normal * kSafeFaceDistance;

    Camera exitPortalCamera = camera;
    exitPortalCamera.position = safeCameraPos;
    exitPortalCamera.baseFov = camera.baseFov;
    exitPortalCamera.lookAt(faceCenter, bitangent);

    setNestedPreviewOverrideFrame(
        buildPreviewOverrideFrame(exitPortalCamera, nestedPreview, childCamera));

    camera.position = safeCameraPos;
    camera.orientation = exitPortalCamera.orientation;

    if (FractalWorld* parentWorld = worldStack.currentWorld()) {
        parentWorld->primeImmediateArea(safeCameraPos, 1);
    }

    transition = PlayerTransition::RISING_OUT;
    transitionTimer = 0.0f;
    transitionStartPos = safeCameraPos;
    transitionEndPos = returnPos;
    transitionStartOrientation = exitPortalCamera.orientation;
    transitionEndOrientation = returnOrientation;
    velocity = glm::vec3(0.0f);
    clearTargetSelection();
}

void Player::beginNestedEntryTransition(WorldStack& worldStack) {
    BiomeSelection nextBiomeSelection{};
    if (!tryPrepareNestedWorld(worldStack, targetBlock, nullptr,
                               &nextBiomeSelection)) {
        return;
    }
    beginNestedPreviewFadeIn(targetBlock, targetNormal);
    if (audioController) {
        audioController->onPortalEntered(targetBlock, nextBiomeSelection.presetId);
    }

    glm::vec3 normal(0.0f), tangent(0.0f), bitangent(0.0f);
    buildPortalBasis(targetNormal, normal, tangent, bitangent);

    const glm::vec3 faceCenter =
        glm::vec3(targetBlock) + glm::vec3(0.5f) + normal * 0.5f;
    const glm::vec3 safeCameraPos = faceCenter + normal * kSafeFaceDistance;

    Camera targetCamera = camera;
    targetCamera.position = safeCameraPos;
    targetCamera.baseFov = camera.baseFov;
    targetCamera.lookAt(faceCenter, bitangent);

    glm::vec3 savedChildPos(0.0f);
    glm::quat savedChildOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    const bool hasSavedChildState =
        worldStack.tryGetNestedPlayerState(targetBlock, savedChildPos, savedChildOrientation);
    if (hasSavedChildState) {
        Camera savedChildCamera = camera;
        savedChildCamera.position = savedChildPos;
        savedChildCamera.orientation = savedChildOrientation;
        enforceSafeNestedSpawn(worldStack, targetBlock, savedChildCamera, false);
        setNestedPreviewOverrideFrame(
            buildPreviewOverrideFrame(targetCamera, nestedPreview, savedChildCamera));
    } else {
        nestedPreview.hasOverrideFrame = false;
        nestedPreview.overrideFrame = NestedPreviewFrame{};
    }

    Camera childCamera = buildNestedPreviewCamera(targetCamera, nestedPreview);
    enforceSafeNestedSpawn(worldStack, targetBlock, childCamera, !hasSavedChildState);

    enterNested.block = targetBlock;
    enterNested.normal = targetNormal;
    enterNested.startPos = camera.position;
    enterNested.targetPos = safeCameraPos;
    enterNested.startOrientation = camera.orientation;
    enterNested.targetOrientation = targetCamera.orientation;
    enterNested.childPos = childCamera.position;
    enterNested.childOrientation = childCamera.orientation;
    enterNested.parentReturnPos = enterNested.startPos;
    enterNested.parentReturnOrientation = enterNested.startOrientation;

    transition = PlayerTransition::DIVING_IN;
    transitionTimer = 0.0f;
    transitionStartPos = enterNested.startPos;
    transitionEndPos = enterNested.targetPos;
    transitionStartOrientation = enterNested.startOrientation;
    transitionEndOrientation = enterNested.targetOrientation;
    velocity = glm::vec3(0.0f);

    clearTargetSelection();
}
