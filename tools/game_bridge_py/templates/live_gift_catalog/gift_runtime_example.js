(function giftRuntimeExampleBootstrap(root) {
    'use strict';

    if (!root.NLP3GiftRuntimeIntegration) {
        throw new Error('Load gift_runtime_integration.js before gift_runtime_example.js');
    }

    // Example game runtime surface. A real game only needs to replace these
    // methods with its own gameplay implementation.
    const gameRuntime = root.NewGameRuntime = root.NewGameRuntime || {
        fireShots(outcome) {
            console.log('[gift] disparos', outcome);
        },
        spawnWave(outcome) {
            console.log('[gift] ola', outcome);
        },
        enableLaser(outcome) {
            console.log('[gift] laser', outcome);
        },
        enableHunter(outcome) {
            console.log('[gift] cazador', outcome);
        },
        triggerNuclear(outcome) {
            console.log('[gift] nuclear', outcome);
        },
        triggerNuclearOverdrive(outcome) {
            console.log('[gift] nuclear overdrive', outcome);
        }
    };

    const handlers = root.NLP3GiftRuntimeIntegration.createDefaultGiftHandlers(gameRuntime);

    const giftIntegration = root.NLP3GiftRuntimeIntegration.createPanelGiftIntegration({
        gameRuntime,
        handlers
    });

    // Example entrypoint for events received from the panel bridge.
    root.onPanelBridgeEvent = function onPanelBridgeEvent(event) {
        return giftIntegration.handlePanelEvent(event);
    };
})(typeof globalThis !== 'undefined' ? globalThis : this);
