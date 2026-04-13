// File: VoxelParadox.Client/src/Systems/gameplay/portal_interaction_system.cpp
// Purpose: centralizes player portal preview and traversal flow behind a reusable gameplay system.
// Flow: Player forwards nested-world preview/transition entry points here while keeping portal state on the Player facade.

// 1. Standard Library
#include <algorithm>

// 2. Third-party Libraries
#include <glm/glm.hpp>

// 3. Local Project Modules
#include "gameplay/portal_interaction_system.hpp"

#include "audio/game_audio_controller.hpp"
#include "gameplay/gameplay_events.hpp"
#include "player/player.hpp"
#include "world/generation/fractal_world.hpp"

namespace Gameplay {

bool PortalInteractionSystem::tryCreatePortalForTargetBlock(
    Player& player,
    WorldStack& worldStack,
    EventQueue* eventQueue
) {
    if (!player.targeting.hasTarget) {
        return false;
    }

    const bool created =
        player.tryPrepareNestedWorld(worldStack, player.targeting.targetBlock);
    if (created) {
        player.firstPortalOpened = true;
        if (eventQueue) {
            eventQueue->emitPortalCreated(
                player.targeting.targetBlock,
                player.targeting.targetNormal
            );
        }
    }

    return created;
}

void PortalInteractionSystem::handleTransition(
    Player& player,
    float dt,
    WorldStack& worldStack
) {
    player.transitionTimer += dt;
    const float t =
        glm::clamp(player.transitionTimer / player.transitionDuration, 0.0f, 1.0f);
    const float smoothT = Player::smoothstep01(t);

    if (player.transition == PlayerTransition::DIVING_IN) {
        player.nestedPreview.active = true;
        player.nestedPreview.fade = glm::clamp(
            player.nestedPreview.fade + dt / player.previewFadeInDuration,
            0.0f,
            1.0f
        );

        player.camera.position = glm::mix(
            player.enterNested.startPos,
            player.enterNested.targetPos,
            smoothT
        );
        player.camera.orientation = glm::normalize(
            glm::slerp(
                player.enterNested.startOrientation,
                player.enterNested.targetOrientation,
                smoothT
            )
        );

        if (t >= 1.0f) {
            finishDiveIn(player, worldStack);
        }
        return;
    }

    if (player.transition == PlayerTransition::RISING_OUT) {
        player.camera.position = glm::mix(
            player.transitionStartPos,
            player.transitionEndPos,
            smoothT
        );
        player.camera.orientation = glm::normalize(
            glm::slerp(
                player.transitionStartOrientation,
                player.transitionEndOrientation,
                smoothT
            )
        );
        player.nestedPreview.active = true;
        player.nestedPreview.fade = 1.0f;
        if (t >= 1.0f) {
            player.transition = PlayerTransition::NONE;
        }
    }
}

void PortalInteractionSystem::updateNestedPreview(
    Player& player,
    WorldStack& worldStack,
    FractalWorld* world,
    float dt
) {
    if (!world) {
        player.clearNestedPreview();
        return;
    }

    const bool lookingAtPortal = player.isLookingAtPortal(world);
    updatePreviewVisibility(player, worldStack, lookingAtPortal, dt);
    preloadNearbyNestedWorld(player, worldStack, world, lookingAtPortal);
}

bool PortalInteractionSystem::beginAscendTransition(
    Player& player,
    WorldStack& worldStack,
    EventQueue* eventQueue
) {
    if (!worldStack.canAscend()) {
        return false;
    }

    const Camera childCamera = player.camera;
    worldStack.saveActivePlayerState(childCamera.position, childCamera.orientation);

    glm::vec3 returnPos(0.0f);
    glm::quat returnOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    glm::ivec3 portalBlock(0);
    glm::ivec3 portalNormal(0);

    if (!worldStack.ascend(returnPos, returnOrientation, portalBlock, portalNormal)) {
        player.transition = PlayerTransition::NONE;
        return false;
    }

    player.showNestedPreviewImmediately(portalBlock, portalNormal);

    glm::vec3 normal(0.0f);
    glm::vec3 tangent(0.0f);
    glm::vec3 bitangent(0.0f);
    Player::buildPortalBasis(portalNormal, normal, tangent, bitangent);

    const glm::vec3 faceCenter =
        glm::vec3(portalBlock) + glm::vec3(0.5f) + normal * 0.5f;
    const glm::vec3 safeCameraPos = faceCenter + normal * Player::kSafeFaceDistance;

    Camera exitPortalCamera = player.camera;
    exitPortalCamera.position = safeCameraPos;
    exitPortalCamera.baseFov = player.camera.baseFov;
    exitPortalCamera.lookAt(faceCenter, bitangent);

    player.setNestedPreviewOverrideFrame(
        Player::buildPreviewOverrideFrame(
            exitPortalCamera,
            player.nestedPreview,
            childCamera
        )
    );

    player.camera.position = safeCameraPos;
    player.camera.orientation = exitPortalCamera.orientation;

    if (FractalWorld* parentWorld = worldStack.currentWorld()) {
        parentWorld->primeImmediateArea(safeCameraPos, 1);
    }

    player.transition = PlayerTransition::RISING_OUT;
    player.transitionTimer = 0.0f;
    player.transitionStartPos = safeCameraPos;
    player.transitionEndPos = returnPos;
    player.transitionStartOrientation = exitPortalCamera.orientation;
    player.transitionEndOrientation = returnOrientation;
    player.velocity = glm::vec3(0.0f);
    player.targeting.clearTargetSelection();

    if (eventQueue) {
        eventQueue->emitUniverseExited(portalBlock, portalNormal);
    }

    return true;
}

void PortalInteractionSystem::beginNestedEntryTransition(
    Player& player,
    WorldStack& worldStack,
    EventQueue* eventQueue
) {
    BiomeSelection nextBiomeSelection{};
    if (!player.tryPrepareNestedWorld(
            worldStack,
            player.targeting.targetBlock,
            nullptr,
            &nextBiomeSelection
        )) {
        return;
    }

    player.beginNestedPreviewFadeIn(
        player.targeting.targetBlock,
        player.targeting.targetNormal
    );

    glm::vec3 normal(0.0f);
    glm::vec3 tangent(0.0f);
    glm::vec3 bitangent(0.0f);
    Player::buildPortalBasis(player.targeting.targetNormal, normal, tangent, bitangent);

    const glm::vec3 faceCenter =
        glm::vec3(player.targeting.targetBlock) + glm::vec3(0.5f) + normal * 0.5f;
    const glm::vec3 safeCameraPos = faceCenter + normal * Player::kSafeFaceDistance;

    Camera targetCamera = player.camera;
    targetCamera.position = safeCameraPos;
    targetCamera.baseFov = player.camera.baseFov;
    targetCamera.lookAt(faceCenter, bitangent);

    glm::vec3 savedChildPos(0.0f);
    glm::quat savedChildOrientation(1.0f, 0.0f, 0.0f, 0.0f);
    const bool hasSavedChildState = worldStack.tryGetNestedPlayerState(
        player.targeting.targetBlock,
        savedChildPos,
        savedChildOrientation
    );

    Camera childCamera = Player::buildNestedPreviewCamera(targetCamera, player.nestedPreview);
    if (hasSavedChildState) {
        childCamera.position = savedChildPos;
        childCamera.orientation = savedChildOrientation;
        player.enforceSafeNestedSpawn(
            worldStack,
            player.targeting.targetBlock,
            childCamera,
            false
        );
        player.setNestedPreviewOverrideFrame(
            Player::buildPreviewOverrideFrame(
                targetCamera,
                player.nestedPreview,
                childCamera
            )
        );
    }
    else {
        player.nestedPreview.hasOverrideFrame = false;
        player.nestedPreview.overrideFrame = Player::NestedPreviewFrame{};
        player.enforceSafeNestedSpawn(
            worldStack,
            player.targeting.targetBlock,
            childCamera,
            true
        );
    }

    player.enterNested.block = player.targeting.targetBlock;
    player.enterNested.normal = player.targeting.targetNormal;
    player.enterNested.startPos = player.camera.position;
    player.enterNested.targetPos = safeCameraPos;
    player.enterNested.startOrientation = player.camera.orientation;
    player.enterNested.targetOrientation = targetCamera.orientation;
    player.enterNested.childPos = childCamera.position;
    player.enterNested.childOrientation = childCamera.orientation;
    player.enterNested.parentReturnPos = player.enterNested.startPos;
    player.enterNested.parentReturnOrientation = player.enterNested.startOrientation;

    player.transition = PlayerTransition::DIVING_IN;
    player.transitionTimer = 0.0f;
    player.transitionStartPos = player.enterNested.startPos;
    player.transitionEndPos = player.enterNested.targetPos;
    player.transitionStartOrientation = player.enterNested.startOrientation;
    player.transitionEndOrientation = player.enterNested.targetOrientation;
    player.velocity = glm::vec3(0.0f);
    player.targeting.clearTargetSelection();

    if (eventQueue) {
        eventQueue->emitUniverseEntered(
            player.enterNested.block,
            player.enterNested.normal,
            nextBiomeSelection.presetId
        );
    }
}

void PortalInteractionSystem::finishDiveIn(
    Player& player,
    WorldStack& worldStack
) {
    if (worldStack.descendInto(
            player.enterNested.block,
            player.enterNested.parentReturnPos,
            player.enterNested.parentReturnOrientation,
            player.enterNested.normal
        )) {
        player.camera.position = player.enterNested.childPos;
        player.camera.orientation = player.enterNested.childOrientation;
    }

    player.velocity = glm::vec3(0.0f);
    player.clearNestedPreview();
    player.transition = PlayerTransition::NONE;
}

void PortalInteractionSystem::updatePreviewVisibility(
    Player& player,
    WorldStack& worldStack,
    bool lookingAtPortal,
    float dt
) {
    if (lookingAtPortal &&
        player.tryPrepareNestedWorld(worldStack, player.targeting.targetBlock)) {
        const bool previewReloaded =
            !player.nestedPreview.active || player.nestedPreview.fade <= 0.0f;
        const bool portalChanged =
            player.nestedPreview.block != player.targeting.targetBlock ||
            player.nestedPreview.normal != player.targeting.targetNormal;
        const bool needsSavedStateAnchor =
            portalChanged || (!player.nestedPreview.hasOverrideFrame && previewReloaded);

        player.activateNestedPreview(
            player.targeting.targetBlock,
            player.targeting.targetNormal,
            player.nestedPreview.fade + dt / player.previewFadeInDuration
        );

        if (needsSavedStateAnchor) {
            player.updateNestedPreviewAnchorFromSavedState(
                worldStack,
                player.targeting.targetBlock,
                player.targeting.targetNormal
            );
        }
        return;
    }

    if (player.nestedPreview.fade <= 0.0f) {
        player.nestedPreview.active = false;
        return;
    }

    player.nestedPreview.fade = glm::clamp(
        player.nestedPreview.fade - dt / player.previewFadeOutDuration,
        0.0f,
        1.0f
    );
    if (player.nestedPreview.fade <= 0.0f) {
        player.nestedPreview.active = false;
    }
}

void PortalInteractionSystem::preloadNearbyNestedWorld(
    Player& player,
    WorldStack& worldStack,
    FractalWorld* world,
    bool lookingAtPortal
) {
    (void)world;

    const bool previewVisible = player.nestedPreview.active || player.nestedPreview.fade > 0.0f;
    if (!lookingAtPortal && !previewVisible) {
        worldStack.clearNestedPreviewWorld();
        return;
    }

    const glm::ivec3 previewBlock = lookingAtPortal
        ? player.targeting.targetBlock
        : player.nestedPreview.block;

    if (!worldStack.hasNestedWorldAtBlock(previewBlock) &&
        !player.tryPrepareNestedWorld(worldStack, previewBlock)) {
        worldStack.clearNestedPreviewWorld();
        return;
    }

    FractalWorld* previewWorld = worldStack.getOrCreateNestedPreviewWorld(previewBlock);
    if (!previewWorld) {
        return;
    }

    (void)previewWorld;
}

}  // namespace Gameplay
