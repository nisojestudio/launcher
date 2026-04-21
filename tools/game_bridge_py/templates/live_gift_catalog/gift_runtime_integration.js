(function giftRuntimeIntegrationBootstrap(root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory(root);
        return;
    }
    root.NLP3GiftRuntimeIntegration = factory(root);
})(typeof globalThis !== 'undefined' ? globalThis : this, function giftRuntimeIntegrationFactory(root) {
    'use strict';

    function getProcessor(explicitProcessor) {
        const processor = explicitProcessor || root.NLP3GiftEventProcessor;
        if (!processor || typeof processor.processGiftEvent !== 'function') {
            throw new Error('NLP3GiftEventProcessor is required before creating the runtime integration');
        }
        return processor;
    }

    function getCatalog(explicitCatalog) {
        const catalog = explicitCatalog || root.NLP3GiftCatalog;
        if (!catalog || typeof catalog !== 'object') {
            throw new Error('NLP3GiftCatalog is required before creating the runtime integration');
        }
        return catalog;
    }

    function ensureHandlerMap(handlers) {
        if (!handlers || typeof handlers !== 'object') {
            throw new Error('Gift runtime integration requires a handlers object');
        }
        return handlers;
    }

    // Conventional gameplay method names. A new game can either implement these
    // methods or pass explicit handlers to override the mapping.
    function createDefaultGiftHandlers(gameRuntime) {
        const runtime = gameRuntime && typeof gameRuntime === 'object' ? gameRuntime : {};
        return {
            disparos: (outcome) => runtime.fireShots ? runtime.fireShots(outcome) : null,
            ola: (outcome) => runtime.spawnWave ? runtime.spawnWave(outcome) : null,
            laser: (outcome) => runtime.enableLaser ? runtime.enableLaser(outcome) : null,
            cazador: (outcome) => runtime.enableHunter ? runtime.enableHunter(outcome) : null,
            nuclear: (outcome) => runtime.triggerNuclear ? runtime.triggerNuclear(outcome) : null,
            nuclear_overdrive: (outcome) => runtime.triggerNuclearOverdrive ? runtime.triggerNuclearOverdrive(outcome) : null,
            default: (outcome) => runtime.fireShots ? runtime.fireShots(outcome) : null
        };
    }

    function createPanelGiftIntegration(options) {
        const settings = options && typeof options === 'object' ? options : {};
        const processor = getProcessor(settings.processor);
        const catalog = getCatalog(settings.catalog);
        const handlers = ensureHandlerMap(settings.handlers || createDefaultGiftHandlers(settings.gameRuntime));
        const fallbackAction = settings.fallbackAction || 'disparos';

        function handlePanelEvent(event) {
            if (!event || typeof event !== 'object') {
                return { ignored: true, reason: 'invalid_event' };
            }

            if (event.kind !== 'gift') {
                return { ignored: true, reason: 'unsupported_kind', kind: event.kind || null };
            }

            // The panel only sends generic gift payloads. The game decides the
            // real gameplay action by resolving giftName + value against its
            // internal catalog and power windows.
            return processor.processGiftEvent(event, {
                catalog,
                handlers,
                fallbackAction,
                powerWindows: settings.powerWindows,
                nuclearMinValue: settings.nuclearMinValue
            });
        }

        // Helper for inbox / websocket / bridge layers. Any source that can
        // register a callback can reuse the same integration.
        function attachToEventSource(subscribe) {
            if (typeof subscribe !== 'function') {
                throw new Error('attachToEventSource expects a subscribe(callback) function');
            }
            return subscribe(handlePanelEvent);
        }

        return Object.freeze({
            catalog,
            handlers,
            handlePanelEvent,
            attachToEventSource
        });
    }

    return Object.freeze({
        createDefaultGiftHandlers,
        createPanelGiftIntegration
    });
});
