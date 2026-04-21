(function giftEventProcessorBootstrap(root, factory) {
    if (typeof module === 'object' && module.exports) {
        module.exports = factory();
        return;
    }
    root.NLP3GiftEventProcessor = factory();
})(typeof globalThis !== 'undefined' ? globalThis : this, function giftEventProcessorFactory() {
    'use strict';

    // Power windows derived from Arena Live v2. These tiers are intentionally
    // simple so any new game can reuse the same trigger logic.
    const POWER_WINDOWS = Object.freeze([
        Object.freeze({ accion: 'disparos', min: 1, max: 19 }),
        Object.freeze({ accion: 'ola', min: 20, max: 29 }),
        Object.freeze({ accion: 'laser', min: 30, max: 99 }),
        Object.freeze({ accion: 'cazador', min: 100, max: 199 }),
        Object.freeze({ accion: 'nuclear', min: 200, max: 499 }),
        Object.freeze({ accion: 'nuclear overdrive', min: 500, max: Number.POSITIVE_INFINITY })
    ]);

    const DEFAULT_FALLBACK_ACTION = 'disparos';
    const NUCLEAR_MIN_VALUE = 200;

    function normalizeGiftName(value) {
        return String(value || '')
            .normalize('NFD')
            .replace(/[\u0300-\u036f]/g, '')
            .trim()
            .toLowerCase();
    }

    function clampPositiveInteger(value, fallback) {
        const parsed = Number(value);
        if (!Number.isFinite(parsed) || parsed <= 0) {
            return fallback;
        }
        return Math.max(1, Math.floor(parsed));
    }

    function normalizeActionKey(value) {
        return String(value || DEFAULT_FALLBACK_ACTION)
            .trim()
            .toLowerCase()
            .replace(/\s+/g, '_');
    }

    function resolvePowerWindows(settings) {
        const candidate = settings && Array.isArray(settings.powerWindows) ? settings.powerWindows : null;
        if (!candidate || candidate.length === 0) {
            return POWER_WINDOWS;
        }

        const normalized = [];
        for (const entry of candidate) {
            if (!entry || typeof entry !== 'object') continue;
            const accion = String(entry.accion || entry.action || '').trim();
            const min = Math.max(0, Math.floor(Number(entry.min) || 0));
            const rawMax = entry.max;
            const max = rawMax === undefined || rawMax === null
                ? Number.POSITIVE_INFINITY
                : Math.max(min, Math.floor(Number(rawMax) || 0));
            if (!accion) continue;
            normalized.push(Object.freeze({ accion, min, max }));
        }

        return normalized.length > 0 ? Object.freeze(normalized) : POWER_WINDOWS;
    }

    function buildNormalizedCatalogIndex(catalog) {
        const index = Object.create(null);
        const source = catalog && typeof catalog === 'object' ? catalog : {};
        for (const [giftName, entry] of Object.entries(source)) {
            index[normalizeGiftName(giftName)] = entry;
        }
        return index;
    }

    function getCatalogEntry(giftName, catalog, normalizedIndex) {
        if (!catalog || typeof catalog !== 'object') {
            return null;
        }

        if (Object.prototype.hasOwnProperty.call(catalog, giftName)) {
            return catalog[giftName];
        }

        const safeName = normalizeGiftName(giftName);
        if (!safeName) {
            return null;
        }

        return normalizedIndex[safeName] || null;
    }

    function resolveActionForValue(totalValue, giftName, fallbackAction, powerWindows) {
        const safeGiftName = normalizeGiftName(giftName);
        const numericValue = Math.max(0, Math.floor(Number(totalValue) || 0));
        const windows = Array.isArray(powerWindows) && powerWindows.length > 0 ? powerWindows : POWER_WINDOWS;

        if (safeGiftName.includes('nuclear') && numericValue > 0) {
            return numericValue >= 500 ? 'nuclear overdrive' : 'nuclear';
        }

        for (const window of windows) {
            if (numericValue >= window.min && numericValue <= window.max) {
                return window.accion;
            }
        }

        return fallbackAction || DEFAULT_FALLBACK_ACTION;
    }

    function resolveGiftOutcome(event, options) {
        const settings = options && typeof options === 'object' ? options : {};
        const data = event && typeof event.data === 'object' && event.data ? event.data : {};
        const fallbackAction = settings.fallbackAction || DEFAULT_FALLBACK_ACTION;
        const nuclearMinValue = Math.max(1, Math.floor(Number(settings.nuclearMinValue) || NUCLEAR_MIN_VALUE));
        const powerWindows = resolvePowerWindows(settings);
        const count = clampPositiveInteger(data.count, 1);
        const giftName = String(data.giftName || data.name || '').trim();
        const normalizedIndex = buildNormalizedCatalogIndex(settings.catalog);
        const catalogEntry = getCatalogEntry(giftName, settings.catalog, normalizedIndex);

        const catalogUnitValue = catalogEntry && Number.isFinite(Number(catalogEntry.valor))
            ? Math.max(0, Math.floor(Number(catalogEntry.valor)))
            : 0;

        const explicitUnitValue = [
            data.coins,
            data.diamond,
            data.coinValue,
            data.diamondValue,
            data.valor
        ].map((value) => Number(value)).find((value) => Number.isFinite(value) && value > 0);

        const unitValue = Math.max(0, Math.floor(explicitUnitValue || catalogUnitValue || 0));
        const rawTotalValue = unitValue * count;
        const specialNuclearTrigger = normalizeGiftName(giftName).includes('nuclear') && rawTotalValue > 0;
        const resolvedValue = specialNuclearTrigger ? Math.max(nuclearMinValue, rawTotalValue) : rawTotalValue;
        const action = resolveActionForValue(resolvedValue, giftName, fallbackAction, powerWindows);

        return {
            kind: 'gift',
            ts: event && event.ts ? event.ts : null,
            user: event && event.user ? event.user : {},
            giftName,
            count,
            catalogHit: Boolean(catalogEntry),
            catalogEntry,
            unitValue,
            totalValue: rawTotalValue,
            resolvedValue,
            baseAction: catalogEntry && catalogEntry.accion ? catalogEntry.accion : fallbackAction,
            action,
            actionKey: normalizeActionKey(action),
            fallbackAction,
            powerWindows,
            specialNuclearTrigger
        };
    }

    // Generic reusable processor. The new game only has to provide handlers for
    // each action tier, plus an optional default handler for unknown gifts.
    function processGiftEvent(event, options) {
        const settings = options && typeof options === 'object' ? options : {};
        const handlers = settings.handlers && typeof settings.handlers === 'object' ? settings.handlers : {};

        if (!event || event.kind !== 'gift') {
            throw new Error('processGiftEvent expects a payload with kind="gift"');
        }

        const outcome = resolveGiftOutcome(event, settings);
        const handler = handlers[outcome.actionKey] || handlers[outcome.action] || handlers.default || null;

        let handlerResult = null;
        if (typeof handler === 'function') {
            handlerResult = handler(outcome, event);
        }

        return {
            ...outcome,
            handled: typeof handler === 'function',
            handlerResult
        };
    }

    return Object.freeze({
        POWER_WINDOWS,
        DEFAULT_FALLBACK_ACTION,
        normalizeGiftName,
        normalizeActionKey,
        resolveActionForValue,
        resolveGiftOutcome,
        processGiftEvent
    });
});
