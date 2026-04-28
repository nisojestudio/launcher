(() => {
  const POLL_STATE_MS = 1000;
  const POLL_REALTIME_MS = 250;
  const POLL_STATE_HIDDEN_MS = 4000;
  const POLL_REALTIME_HIDDEN_MS = 1500;
  const STORAGE_KEY = "nlp3-compact-dashboard-preferences";
  const AUTH_FORM_STORAGE_KEY = "nlp3-auth-form-v1";
  const AUTH_FORM_LEGACY_STORAGE_KEYS = [
    "nlp3-auth-form",
    "nisoje-auth-form",
    "nisoje-remote-access",
    "livepanel-auth-form",
  ];
  const VOICE_NOTICES_STORAGE_KEY = "nlp3-custom-voice-notices-v1";
  const AUDIO_NOTICE_MAX_BYTES = 1572864;
  const MAX_RECENT_ITEMS = 20;
  const FREQUENCY_INTERVAL_MAP = {
    low: "120000",
    normal: "60000",
    high: "30000",
  };
  const NOTICE_TRIGGERS = [
    { value: "gift", label: "Regalo" },
    { value: "share", label: "Compartir" },
    { value: "follow", label: "Seguir" },
    { value: "like", label: "Like" },
    { value: "subscription", label: "Suscripci\u00f3n" },
    { value: "timer", label: "Temporizado" },
  ];
  const NOTICE_CONTENT_TYPES = [
    { value: "text", label: "Mensaje" },
    { value: "audio", label: "Audio" },
  ];
  const COMMUNITY_ACTIVITY_LABELS = new Set([
    "chat_message",
    "gift",
    "like",
    "follow",
    "viewer_join",
    "share",
  ]);
  const HIDDEN_GAME_IDS = new Set(["null-game", "event-counter"]);
  const ACTIVITY_GIFT_PRESETS = [
    { value: "rose", name: "ROSE", coins: 1, icon: "\ud83c\udf39" },
    { value: "perfume", name: "PERFUME", coins: 20, icon: "\ud83e\uddf4" },
    { value: "crown", name: "CROWN", coins: 99, icon: "\ud83d\udc51" },
    { value: "tiktok", name: "TIKTOK", coins: 1, icon: "\ud83c\udfb5" },
    { value: "heart_me", name: "HEART ME", coins: 25, icon: "\ud83d\udc96" },
    { value: "galaxy", name: "GALAXY", coins: 1000, icon: "\ud83c\udf0c" },
  ];
  const ARENA_PREVIEW_IMAGE = `data:image/jpeg;base64,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`;
  const GAME_PREVIEW_IMAGES = window.__GAME_PREVIEW_IMAGES__ || {};
  const GAME_PRESENTATION_OVERRIDES = {
    "versus": {
      title: "Versus",
      previewLabel: "Vs",
      previewClass: "preview-versus",
      previewImage: GAME_PREVIEW_IMAGES.versus || "",
      description: "Batalla 3:4 por equipos con likes, regalos, follows y shares.",
      meta: [],
    },
    "arena_live": {
      title: "Arena Live",
      previewLabel: "Arena",
      previewClass: "preview-arena",
      previewImage: GAME_PREVIEW_IMAGES.arena_live || ARENA_PREVIEW_IMAGE,
      description: "Regalos del live te dan poderes y puntos para subir.",
      meta: [],
    },
    "super_chat": {
      title: "Super Chat",
      previewLabel: "Chat",
      previewClass: "preview-superchat",
      previewImage: GAME_PREVIEW_IMAGES.super_chat || "",
      description: "Revienta esferas, esquiva el peligro y suma puntos.",
      meta: [],
    },
    "conquista": {
      title: "Conquista",
      previewLabel: "Mapa",
      previewClass: "preview-conquest",
      previewImage: GAME_PREVIEW_IMAGES.conquista || "",
      description: "El live pelea por ganar terreno hasta dominar todo el mapa.",
      meta: [],
    },
  };

  const state = {
    payload: null,
    eventsPayload: { total: 0, items: [] },
    metricsPayload: null,
    busyState: false,
    busyRealtime: false,
    statePollTimerId: 0,
    realtimePollTimerId: 0,
    voiceDirty: false,
    voiceNotices: loadVoiceNotices(),
    voiceNoticesHydrated: false,
    noticeAccordionOpenIds: new Set(),
    noticeAccordionInitialized: false,
    uiPrefs: loadUiPrefs(),
    authDraft: loadAuthDraft(),
    uiInteractionHoldUntil: 0,
    stickyMetrics: {
      viewers: 0,
    },
    authUi: {
      busy: false,
      feedback: "",
      feedbackTone: "warn",
    },
    support: {
      autoExportedIssueKeys: new Set(),
    },
    recentActivityMarkup: "",
    gamesMarkup: "",
    advancedLogText: "",
    activityClearBeforeMs: 0,
    selectedGiftValue: ACTIVITY_GIFT_PRESETS[0].value,
    terminalLines: [
      "Nisoje Studio est\u00e1 listo.",
      "Conecta tu cuenta para empezar el live.",
    ],
  };

  const $ = (selector) => document.querySelector(selector);

  const els = {
    connectionPill: $("#connection-pill"),
    connectionStatusText: $("#connection-status-text"),
    connectionStatusMeta: $("#connection-status-meta"),
    connectionTargetUser: $("#connection-target-user"),
    liveRoom: $("#live-room"),
    connectionNote: $("#connection-note"),
    connectForm: $("#connect-form"),
    tiktokUser: $("#tiktok-user"),
    connectButton: $("#connect-button"),
    disconnectButton: $("#disconnect-button"),
    authGate: $("#auth-gate"),
    authForm: $("#auth-form"),
    authEmail: $("#auth-email"),
    authPassword: $("#auth-password"),
    authLicenseKey: $("#auth-license-key"),
    authFeedback: $("#auth-feedback"),
    authSubmitButton: $("#auth-submit-button"),
    authSupportButton: $("#auth-support-button"),
    authStatePill: $("#auth-state-pill"),
    authServerMessage: $("#auth-server-message"),
    authMetaGrid: $("#auth-meta-grid"),
    authAccountLabel: $("#auth-account-label"),
    authLicenseLabel: $("#auth-license-label"),
    authValidatedAt: $("#auth-validated-at"),
    authSessionRow: $("#auth-session-row"),
    authSessionNote: $("#auth-session-note"),
    authLogoutButton: $("#auth-logout-button"),

    metricViewers: $("#metric-viewers"),
    metricLikes: $("#metric-likes"),
    metricGifts: $("#metric-gifts"),
    metricMessages: $("#metric-messages"),
    metricFollowers: $("#metric-followers"),
    metricShares: $("#metric-shares"),
    streamUpdated: $("#stream-updated"),
    metricsResetButton: $("#metrics-reset-button"),

    voiceEnabled: $("#voice-enabled"),
    voiceForm: $("#voice-form"),
    voiceProfile: $("#voice-profile"),
    voiceLanguage: $("#voice-language"),
    voiceNoticesList: $("#voice-notices-list"),
    voiceAddNoticeButton: $("#voice-add-notice"),
    voiceFrequency: $("#voice-frequency"),
    voiceSaveButton: $("#voice-save-button"),
    voiceReadChatVisible: $("#voice-read-chat-visible"),
    chatReadingRowVisible: $("#chat-reading-row-visible"),
    chatReadingScopeVisible: $("#chat-reading-scope-visible"),
    voiceReadGifts: $("#voice-read-gifts"),
    voiceReadFollows: $("#voice-read-follows"),
    voiceReadLikes: $("#voice-read-likes"),
    voiceReadSubscribers: $("#voice-read-subscribers"),
    voiceReadShares: $("#voice-read-shares"),
    voiceReadChat: $("#voice-read-chat"),
    voicePeriodicInterval: $("#voice-periodic-interval"),
    chatReadingRow: $("#chat-reading-row"),
    chatReadingScope: $("#chat-reading-scope"),
    voiceSpeakButton: $("#voice-speak-button"),
    templateSummaryText: $("#template-summary-text"),

    messageGiftTemplate: $("#message-gift-template"),
    messageChatTemplate: $("#message-chat-template"),
    messageFollowTemplate: $("#message-follow-template"),
    messageLikeTemplate: $("#message-like-template"),
    messageSubscriberTemplate: $("#message-subscriber-template"),
    messageShareTemplate: $("#message-share-template"),
    messagePeriodicList: $("#message-periodic-list"),

    gamesList: $("#games-list"),
    gamesCount: $("#games-count"),

    titlebarLatencyPill: $("#titlebar-latency-pill"),
    statusLatency: $("#status-latency"),
    titlebarClockText: $("#titlebar-clock-text"),
    statusLastEvent: $("#status-last-event"),
    assistantSummaryPill: $("#assistant-summary-pill"),
    assistantSummaryDot: $("#assistant-summary-dot"),
    assistantSummaryText: $("#assistant-summary-text"),
    assistantSummaryCopy: $("#assistant-summary-copy"),
    assistantTiktokState: $("#assistant-tiktok-state"),
    assistantLiveState: $("#assistant-live-state"),
    recentActivityList: $("#recent-activity-list"),
    activityUpdated: $("#activity-updated"),
    activityFeedUser: $("#activity-feed-user"),
    activityFeedLastType: $("#activity-feed-last-type"),
    activityClearButton: $("#activity-clear-button"),
    activityChatInput: $("#activity-chat-input"),
    activityChatButton: $("#activity-chat-button"),
    activityGiftPicker: $("#activity-gift-picker"),
    activityGiftMenu: $("#activity-gift-menu"),
    activitySelectedGiftIcon: $("#activity-selected-gift-icon"),
    activitySelectedGiftName: $("#activity-selected-gift-name"),
    activityGiftButton: $("#activity-gift-button"),
    activityShareButton: $("#activity-share-button"),
    activityFollowButton: $("#activity-follow-button"),
    activityLikeCount: $("#activity-like-count"),
    activityLikeButton: $("#activity-like-button"),
    reconnectButton: $("#reconnect-button"),
    refreshButton: $("#refresh-button"),
    gameRuntimeNote: $("#game-runtime-note"),
    titlebarDragRegion: $("#titlebar-drag-region"),
    windowMinimizeButton: $("#window-minimize-button"),
    windowMaximizeButton: $("#window-maximize-button"),
    windowCloseButton: $("#window-close-button"),
  };

  const SAMPLE_AVATAR_DATA_URL =
    "data:image/svg+xml;utf8,"
    + encodeURIComponent(
      "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' viewBox='0 0 96 96'>"
      + "<rect width='96' height='96' rx='24' fill='#10253d'/>"
      + "<circle cx='48' cy='36' r='18' fill='#7dd3fc'/>"
      + "<path d='M20 82c4-18 18-28 28-28s24 10 28 28' fill='#34d399'/>"
      + "</svg>"
    );

  function loadUiPrefs() {
    const fallback = {
      voiceProfile: "spanish-neutral",
      voiceLanguage: "es",
      voiceFrequency: "normal",
      chatReadingScope: "everyone",
      readChat: false,
    };
    try {
      const raw = window.localStorage.getItem(STORAGE_KEY);
      if (!raw) {
        return fallback;
      }
      const parsed = JSON.parse(raw);
      return {
        voiceProfile: typeof parsed.voiceProfile === "string" ? parsed.voiceProfile : fallback.voiceProfile,
        voiceLanguage: typeof parsed.voiceLanguage === "string" ? parsed.voiceLanguage : fallback.voiceLanguage,
        voiceFrequency: typeof parsed.voiceFrequency === "string" ? parsed.voiceFrequency : fallback.voiceFrequency,
        chatReadingScope: typeof parsed.chatReadingScope === "string" ? parsed.chatReadingScope : fallback.chatReadingScope,
        readChat: typeof parsed.readChat === "boolean" ? parsed.readChat : fallback.readChat,
      };
    } catch (_) {
      return fallback;
    }
  }

  function loadAuthDraft() {
    const fallback = {
      email: "",
      password: "",
      licenseKey: "",
    };
    const directDraft = readStoredAuthDraft(AUTH_FORM_STORAGE_KEY);
    if (directDraft) {
      return directDraft;
    }

    for (const legacyKey of AUTH_FORM_LEGACY_STORAGE_KEYS) {
      const legacyDraft = readStoredAuthDraft(legacyKey);
      if (!legacyDraft) {
        continue;
      }
      try {
        window.localStorage.setItem(AUTH_FORM_STORAGE_KEY, JSON.stringify(legacyDraft));
      } catch (_) {
        // Ignore local storage issues during migration.
      }
      return legacyDraft;
    }

    const recoveredDraft = recoverLegacyAuthDraft();
    if (recoveredDraft) {
      try {
        window.localStorage.setItem(AUTH_FORM_STORAGE_KEY, JSON.stringify(recoveredDraft));
      } catch (_) {
        // Ignore local storage issues during migration.
      }
      return recoveredDraft;
    }

    return fallback;
  }

  function readStoredAuthDraft(storageKey) {
    if (!storageKey) {
      return null;
    }
    try {
      const raw = window.localStorage.getItem(storageKey);
      if (!raw) {
        return null;
      }
      const parsed = JSON.parse(raw);
      const draft = {
        email: typeof parsed.email === "string" ? parsed.email.trim() : "",
        password: typeof parsed.password === "string" ? parsed.password : "",
        licenseKey: typeof parsed.licenseKey === "string"
          ? normalizeLicenseKey(parsed.licenseKey)
          : "",
      };
      return draft.email || draft.password || draft.licenseKey ? draft : null;
    } catch (_) {
      return null;
    }
  }

  function recoverLegacyAuthDraft() {
    try {
      for (let index = 0; index < window.localStorage.length; index += 1) {
        const storageKey = window.localStorage.key(index);
        if (!storageKey || storageKey === AUTH_FORM_STORAGE_KEY) {
          continue;
        }
        const recovered = readStoredAuthDraft(storageKey);
        const populatedFields = [
          recovered?.email,
          recovered?.password,
          recovered?.licenseKey,
        ].filter((value) => String(value || "").trim()).length;
        if (recovered && populatedFields >= 2) {
          return recovered;
        }
      }
    } catch (_) {
      return null;
    }

    return null;
  }

  function saveAuthDraft() {
    try {
      const payload = {
        email: String(state.authDraft?.email || "").trim(),
        password: String(state.authDraft?.password || ""),
        licenseKey: normalizeLicenseKey(state.authDraft?.licenseKey),
      };
      window.localStorage.setItem(AUTH_FORM_STORAGE_KEY, JSON.stringify(payload));
    } catch (_) {
      // Ignore local storage issues.
    }
  }

  function syncAuthDraft(partial = {}) {
    state.authDraft = {
      email: typeof partial.email === "string"
        ? partial.email.trim()
        : String(state.authDraft?.email || "").trim(),
      password: typeof partial.password === "string"
        ? partial.password
        : String(state.authDraft?.password || ""),
      licenseKey: typeof partial.licenseKey === "string"
        ? normalizeLicenseKey(partial.licenseKey)
        : normalizeLicenseKey(state.authDraft?.licenseKey),
    };
    saveAuthDraft();
  }

  function sanitizeVoiceNotice(raw, index = 0) {
    const allowedTriggers = new Set(NOTICE_TRIGGERS.map((item) => item.value));
    const allowedContentTypes = new Set(NOTICE_CONTENT_TYPES.map((item) => item.value));
    const trigger = allowedTriggers.has(raw?.trigger) ? raw.trigger : "gift";
    const contentType = allowedContentTypes.has(raw?.contentType) ? raw.contentType : "text";
    const seconds = Math.max(5, Number.parseInt(raw?.seconds, 10) || 30);

    return {
      id: String(raw?.id || `notice-${Date.now()}-${index}-${Math.random().toString(36).slice(2, 7)}`),
      trigger,
      contentType,
      message: normalizeLegacyNoticeMessage(trigger, raw?.message || ""),
      seconds,
      audioName: String(raw?.audioName || ""),
      audioMimeType: String(raw?.audioMimeType || ""),
      audioDataUrl: typeof raw?.audioDataUrl === "string" ? raw.audioDataUrl : "",
    };
  }

  function normalizeLegacyNoticeMessage(trigger, rawValue) {
    const message = String(rawValue || "").trim();
    if (!message) {
      return "";
    }

    const normalized = message.toLowerCase();
    if (trigger === "subscription" && normalized === "thanks {user} for subscribing") {
      return "Gracias {user} por suscribirte";
    }
    if (trigger === "timer" && normalized === "remember to follow") {
      return "Recuerda seguir la cuenta";
    }
    if (trigger === "follow" && normalized === "thanks for following the account") {
      return "Gracias por seguir la cuenta";
    }
    if (trigger === "share" && normalized === "thanks for sharing the live") {
      return "Gracias por compartir el directo";
    }
    return message;
  }

  function loadVoiceNotices() {
    try {
      const raw = window.localStorage.getItem(VOICE_NOTICES_STORAGE_KEY);
      if (!raw) {
        return [];
      }
      const parsed = JSON.parse(raw);
      if (!Array.isArray(parsed)) {
        return [];
      }
      return parsed.map((item, index) => sanitizeVoiceNotice(item, index));
    } catch (_) {
      return [];
    }
  }

  function saveVoiceNotices() {
    try {
      const payload = state.voiceNotices.map((notice, index) => sanitizeVoiceNotice(notice, index));
      window.localStorage.setItem(VOICE_NOTICES_STORAGE_KEY, JSON.stringify(payload));
    } catch (_) {
      // Ignore local storage issues.
    }
  }

  function createVoiceNotice(partial = {}) {
    return sanitizeVoiceNotice(partial, state.voiceNotices.length);
  }

  function saveUiPrefs() {
    try {
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state.uiPrefs));
    } catch (_) {
      // Ignore local storage issues.
    }
  }

  function escapeHtml(value) {
    return String(value ?? "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;")
      .replaceAll("'", "&#39;");
  }

  function setText(element, value, options = {}) {
    if (!element) {
      return;
    }
    const text = String(value ?? "");
    if (element.textContent === text) {
      return;
    }
    element.textContent = text;
    if (options.animate) {
      element.classList.remove("updating");
      void element.offsetWidth;
      element.classList.add("updating");
    }
  }

  function setHtml(element, value, cacheKey) {
    if (!element) {
      return;
    }
    const html = String(value ?? "");
    if ((cacheKey ? state[cacheKey] === html : true) && element.innerHTML === html) {
      return;
    }
    element.innerHTML = html;
    if (cacheKey) {
      state[cacheKey] = html;
    }
  }

  function setPillTone(element, tone) {
    if (!element) {
      return;
    }
    element.classList.remove("tone-live", "tone-warn", "tone-danger");
    if (tone) {
      element.classList.add(`tone-${tone}`);
    }
  }

  function authSnapshot(payload = state.payload) {
    return payload?.snapshot?.auth || {
      required: false,
      authenticated: true,
      email: "",
      firebaseUid: "",
      licenseKey: "",
      message: "",
      lastErrorCode: "",
      lastValidatedTimestampMs: 0,
    };
  }

  function authIsLocked(payload = state.payload) {
    const auth = authSnapshot(payload);
    return !!auth.required && !auth.authenticated;
  }

  function resolveTimestampMs(value) {
    const parsed = Number(value || 0);
    if (!Number.isFinite(parsed) || parsed <= 0) {
      return 0;
    }
    return parsed < 10_000_000_000 ? parsed * 1000 : parsed;
  }

  function formatDateTime(timestampMs) {
    const resolvedTimestamp = resolveTimestampMs(timestampMs);
    if (!resolvedTimestamp) {
      return "Nunca";
    }
    try {
      return new Date(resolvedTimestamp).toLocaleString([], {
        year: "numeric",
        month: "2-digit",
        day: "2-digit",
        hour: "2-digit",
        minute: "2-digit",
      });
    } catch (_) {
      return "Nunca";
    }
  }

  function normalizeLicenseKey(value) {
    return String(value || "").trim().toUpperCase();
  }

  function syncViewportMetrics() {
    const viewportHeight = Math.max(
      Number(window.visualViewport?.height) || 0,
      Number(window.innerHeight) || 0,
      Number(document.documentElement?.clientHeight) || 0
    );
    if (viewportHeight > 0) {
      document.documentElement.style.setProperty("--app-viewport-height", `${Math.round(viewportHeight)}px`);
    }
  }

  function setAuthFeedback(message, tone = "warn") {
    state.authUi.feedback = String(message || "").trim();
    state.authUi.feedbackTone = tone;
    if (state.payload) {
      renderAuth(state.payload);
    }
  }

  function clearAuthFeedback() {
    state.authUi.feedback = "";
    state.authUi.feedbackTone = "warn";
  }

  function responseNeedsAuth(response) {
    return response?.errorCode === "auth_required" || response?.message === "auth_required";
  }

  function buildAuthDeviceName() {
    const platform = navigator.userAgentData?.platform || navigator.platform || "Windows";
    const locale = navigator.language || "";
    return [platform, locale].filter(Boolean).join(" · ");
  }

  function buildAuthDeviceId() {
    return [window.location.hostname || "127.0.0.1", navigator.userAgent || "embedded-ui"]
      .filter(Boolean)
      .join(" | ")
      .slice(0, 255);
  }

  function focusAuthField() {
    if (!authIsLocked() || state.authUi.busy) {
      return;
    }
    const target =
      (!String(els.authEmail?.value || "").trim() && els.authEmail)
      || (!String(els.authPassword?.value || "").trim() && els.authPassword)
      || (!normalizeLicenseKey(els.authLicenseKey?.value) && els.authLicenseKey)
      || els.authEmail;
    target?.focus();
    target?.select?.();
  }

  function authDialogHasActiveInput() {
    return !!document.activeElement?.closest?.(".auth-dialog");
  }

  function normalizedUser(value) {
    return String(value || "").trim().replace(/^@+/, "");
  }

  function appendLog(text) {
    const line = String(text ?? "").trim();
    if (!line) {
      return;
    }
    const stamp = new Date().toLocaleTimeString([], {
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
    state.terminalLines.unshift(`[${stamp}] ${line}`);
    state.terminalLines = state.terminalLines.slice(0, 24);
    renderAdvancedLogs();
  }

  function renderAdvancedLogs() {
    if (!els.advancedLogOutput) {
      return;
    }
    const runnerLines = state.payload?.snapshot?.externalBridge?.runnerRecentLogLines || [];
    const gameLines = state.payload?.snapshot?.externalGame?.recentLogs || [];
    const text = [...state.terminalLines, ...runnerLines, ...gameLines].join("\n");
    if (state.advancedLogText === text) {
      return;
    }
    els.advancedLogOutput.textContent = text;
    state.advancedLogText = text;
  }

  function supportsHostWindowControls() {
    return typeof window.chrome?.webview?.postMessage === "function";
  }

  function sendWindowAction(action) {
    if (!supportsHostWindowControls()) {
      return false;
    }
    window.chrome.webview.postMessage(`window:${action}`);
    return true;
  }

  function activityGiftPreset(value = state.selectedGiftValue) {
    return ACTIVITY_GIFT_PRESETS.find((item) => item.value === value) || ACTIVITY_GIFT_PRESETS[0];
  }

  function selectActivityGift(value) {
    const preset = activityGiftPreset(value);
    state.selectedGiftValue = preset.value;
    if (els.activitySelectedGiftIcon) {
      setText(els.activitySelectedGiftIcon, preset.icon);
    }
    if (els.activitySelectedGiftName) {
      setText(els.activitySelectedGiftName, `${preset.name} (${preset.coins} coin${preset.coins === 1 ? "" : "s"})`);
    }
    if (els.activityGiftPicker?.open) {
      els.activityGiftPicker.open = false;
    }
  }

  function renderActivityGiftMenu() {
    if (!els.activityGiftMenu) {
      return;
    }
    els.activityGiftMenu.innerHTML = ACTIVITY_GIFT_PRESETS.map((preset) => (
      `<button class="gift-picker-option" type="button" data-gift-value="${escapeHtml(preset.value)}">` +
        `<span class="gift-picker-option-icon">${escapeHtml(preset.icon)}</span>` +
        `<span class="gift-picker-option-name">` +
          `<strong>${escapeHtml(preset.name)}</strong>` +
          `<span>${escapeHtml(`${preset.coins} coin${preset.coins === 1 ? "" : "s"}`)}</span>` +
        `</span>` +
        `<span class="gift-picker-option-points">${escapeHtml(`${preset.coins}`)}</span>` +
      `</button>`
    )).join("");
    selectActivityGift(state.selectedGiftValue);
  }

  function formatTime(timestampMs) {
    const resolvedTimestamp = resolveTimestampMs(timestampMs);
    if (!resolvedTimestamp) {
      return "Nunca";
    }
    try {
      return new Date(resolvedTimestamp).toLocaleTimeString([], {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
      });
    } catch (_) {
      return "Nunca";
    }
  }

  function formatCurrentTime() {
    try {
      return new Date().toLocaleTimeString([], {
        hour: "2-digit",
        minute: "2-digit",
        second: "2-digit",
      });
    } catch (_) {
      return "--:--:--";
    }
  }

  function compactText(value, fallback = "") {
    const text = String(value || "").trim();
    if (!text) {
      return fallback;
    }
    return text.length > 42 ? `${text.slice(0, 39).trim()}...` : text;
  }

  function updateTitlebarClock() {
    setText(els.titlebarClockText, formatCurrentTime());
  }

  function formatLatency(value) {
    return `${Number(value || 0)} ms`;
  }

  function latencyPresentation(value, connected) {
    const rawLatency = Number(value);
    const latencyMs = Math.max(0, rawLatency || 0);
    if (connected && (!Number.isFinite(rawLatency) || latencyMs <= 0)) {
      return { text: "Escuchando", tone: "live", hidden: false };
    }
    if (!connected && (!Number.isFinite(rawLatency) || latencyMs <= 0 || latencyMs > 600000)) {
      return { text: "Sin conectar", tone: "warn", hidden: true };
    }
    if (!Number.isFinite(latencyMs) || latencyMs > 600000) {
      return { text: "Sin se\u00f1al", tone: "danger", hidden: false };
    }
    if (latencyMs < 1000) {
      return { text: `${Math.round(latencyMs)} ms`, tone: "live", hidden: false };
    }
    if (latencyMs < 5000) {
      return { text: `${(latencyMs / 1000).toFixed(1)} s`, tone: "warn", hidden: false };
    }
    return { text: `${Math.round(latencyMs / 1000)} s`, tone: "danger", hidden: false };
  }

  function displayMetric(value, hasSignal) {
    if (!hasSignal && !value) {
      return "--";
    }
    return String(value ?? "0");
  }

  function toneForVoiceProfile(profile) {
    if (profile === "english-male") {
      return "electric";
    }
    if (profile === "spanish-female" || profile === "english-female") {
      return "warm";
    }
    return "neutral";
  }

  function localizedVoiceDisplayName(voice) {
    const byId = {
      "spanish-female": "Espa\u00f1ol femenina",
      "spanish-male": "Espa\u00f1ol masculina",
      "spanish-neutral": "Espa\u00f1ol neutra",
      "english-female": "Ingl\u00e9s femenina",
      "english-male": "Ingl\u00e9s masculina",
    };
    return byId[voice?.id] || voice?.displayName || voice?.id || "Voz";
  }

  function normalizedPeriodicIntervalValue(intervalMs) {
    const interval = Number(intervalMs || 60000);
    if (interval <= 30000) {
      return "30000";
    }
    if (interval >= 120000) {
      return "120000";
    }
    return "60000";
  }

  function anyVoiceOptionEnabled() {
    return [
      els.voiceReadGifts.checked,
      els.voiceReadFollows.checked,
      els.voiceReadLikes.checked,
      els.voiceReadSubscribers.checked,
      els.voiceReadShares.checked,
      els.voiceReadChat.checked,
    ].some(Boolean);
  }

  function noticeTriggerLabel(trigger) {
    return NOTICE_TRIGGERS.find((item) => item.value === trigger)?.label || "Aviso";
  }

  function noticeTypeLabel(notice) {
    return notice?.contentType === "audio" ? "Audio" : "Mensaje";
  }

  function noticeSubtitle(notice) {
    if (!notice) {
      return "";
    }
    if (notice.trigger === "timer") {
      return `Se reproduce cada ${notice.seconds}s`;
    }
    return notice.contentType === "audio"
      ? "Audio personalizado"
      : "Mensaje personalizado";
  }

  function textNoticeCount() {
    return state.voiceNotices.filter((notice) => notice.contentType === "text").length;
  }

  function timerNoticeCount() {
    return state.voiceNotices.filter((notice) => notice.trigger === "timer").length;
  }

  function updateTemplateSummary() {
    const count = state.voiceNotices.length;
    const audioCount = state.voiceNotices.filter((notice) => notice.contentType === "audio").length;
    const periodicCount = timerNoticeCount();
    if (!els.templateSummaryText) {
      return;
    }
    if (!count) {
      setText(els.templateSummaryText, "Sin mensajes");
      return;
    }
    const fragments = [`${count} ${count === 1 ? "mensaje" : "mensajes"}`];
    if (audioCount) {
      fragments.push(`${audioCount} ${audioCount === 1 ? "audio" : "audios"}`);
    }
    if (periodicCount) {
      fragments.push(`${periodicCount} temporizados`);
    }
    if (state.voiceDirty) {
      fragments.push("cambios sin aplicar");
    }
    setText(els.templateSummaryText, fragments.join(", "));
  }

  function renderVoiceSaveState() {
    if (!els.voiceSaveButton) {
      return;
    }
    const dirty = !!state.voiceDirty;
    els.voiceSaveButton.disabled = !dirty;
    setText(els.voiceSaveButton, "Aplicar cambios");
  }

  function updateChatReadingVisibility() {
    const enabled = els.voiceEnabled.checked && !!els.voiceReadChatVisible?.checked;
    if (els.voiceReadChat) {
      els.voiceReadChat.checked = enabled;
    }
    if (els.chatReadingScope && els.chatReadingScopeVisible) {
      els.chatReadingScope.value = els.chatReadingScopeVisible.value;
    }
    els.chatReadingScope.disabled = !enabled;
    els.chatReadingScopeVisible.disabled = !enabled;
    els.chatReadingRow.classList.toggle("is-disabled", !enabled);
    els.chatReadingRowVisible?.classList.toggle("is-disabled", !enabled);
  }

  function holdUiInteraction(ms = 2200) {
    state.uiInteractionHoldUntil = Math.max(
      Number(state.uiInteractionHoldUntil || 0),
      Date.now() + Math.max(250, Number(ms || 0))
    );
  }

  function uiInteractionLocked() {
    if (Date.now() < Number(state.uiInteractionHoldUntil || 0)) {
      return true;
    }
    const active = document.activeElement;
    return !!active?.closest?.(".voice-panel, .assistant-panel, .activity-panel, .auth-dialog");
  }

  function humanizeRunnerIssue(external) {
    const runtimeSummary = String(external?.runtimeSummary || "").trim();
    if (external?.runtimeChecked && external?.runtimeReady === false && runtimeSummary) {
      return runtimeSummary;
    }

    const raw = String(
      external?.runnerLastError
      || ((external?.connectionState === "faulted" || external?.connectionState === "disconnected")
        ? external?.lastStatusMessage
        : "")
      || ""
    ).trim();
    if (!raw) {
      return "";
    }

    const lower = raw.toLowerCase();
    if (lower.includes("runner script not found")) {
      return "No se encontr\u00f3 el bridge de TikTok en esta instalaci\u00f3n.";
    }
    if (lower.includes("access is denied")) {
      return "Windows bloque\u00f3 el arranque del bridge de TikTok. Revisa permisos, antivirus o reinstala el panel.";
    }
    if (lower.includes("could not create runner pipes")) {
      return "No se pudieron crear los canales internos del bridge de TikTok.";
    }
    if (lower.includes("could not start external runner process")) {
      return "No se pudo iniciar el bridge externo de TikTok.";
    }
    if (lower.includes("usernotfounderror") || lower.includes("user not found") || lower.includes("no se encontro ese usuario")) {
      return "No se encontr\u00f3 ese usuario en TikTok. Revisa que el @ est\u00e9 bien escrito y que sea el username real.";
    }
    if (lower.includes("stream_disconnected") || lower.includes("la conexion con tiktok se cerro")) {
      return "TikTok cerr\u00f3 la sesi\u00f3n poco despu\u00e9s de abrirla. Revisa si la cuenta est\u00e1 realmente en vivo.";
    }
    if (lower.includes("no se pudo cargar tiktoklive")) {
      return "El runtime del bridge no pudo cargar TikTokLive en esta instalaci\u00f3n.";
    }
    return raw;
  }

  async function exportSupportBundle(reason = "manual", options = {}) {
    try {
      const response = await apiPostJson("/api/support/export", { reason });
      if (response?.ok) {
        if (!options.silent) {
          appendLog(`Soporte exportado: ${response.path}`);
        }
        return response;
      }
      if (!options.silent) {
        appendLog(response?.message || "No se pudo exportar el soporte.");
      }
      return response;
    } catch (error) {
      if (!options.silent) {
        appendLog(`No se pudo exportar el soporte: ${error}`);
      }
      return { ok: false, message: String(error || "") };
    }
  }

  async function maybeAutoExportSupportBundle(issueKey, reason, details = "") {
    if (!issueKey || state.support.autoExportedIssueKeys.has(issueKey)) {
      return;
    }
    state.support.autoExportedIssueKeys.add(issueKey);
    const response = await exportSupportBundle(reason, { silent: true });
    if (response?.ok && details) {
      appendLog(`${details} Soporte guardado en ${response.path}`);
    } else if (details) {
      appendLog(details);
    }
  }

  function detectIssueTransitions(payload) {
    const auth = payload?.snapshot?.auth || {};
    if (!auth.authenticated && auth.lastErrorCode && auth.message) {
      const authIssueKey = `auth:${auth.lastErrorCode}:${auth.message}`;
      void maybeAutoExportSupportBundle(
        authIssueKey,
        "auth_access_failed",
        `Diagn\u00f3stico acceso: ${auth.message}`
      );
    }

    const external = payload?.snapshot?.externalBridge || {};
    if (external?.runtimeChecked && external?.runtimeReady === false && external?.runtimeSummary) {
      const runtimeIssueKey = [
        external.runtimeSummary,
        ...(external.runtimeAlerts || []),
      ].join("|");
      void maybeAutoExportSupportBundle(
        `runtime:${runtimeIssueKey}`,
        "tiktok_runtime_missing",
        `Diagnóstico TikTok: ${external.runtimeSummary}`
      );
    }

    const runnerIssue = humanizeRunnerIssue(external);
    const connected = external.connectionState === "connected" || !!external.runnerRunning;
    if (!connected && runnerIssue && (external.runnerLastError || external.connectionState === "faulted")) {
      const runnerIssueKey = [
        external.targetUser || "",
        external.connectionState || "",
        runnerIssue,
      ].join("|");
      void maybeAutoExportSupportBundle(
        `runner:${runnerIssueKey}`,
        "tiktok_connection_failed",
        `Diagn\u00f3stico TikTok: ${runnerIssue}`
      );
    }
  }

  function renderVoiceNotices() {
    if (!els.voiceNoticesList) {
      return;
    }

    if (!state.voiceNotices.length) {
      state.noticeAccordionOpenIds.clear();
      state.noticeAccordionInitialized = false;
      els.voiceNoticesList.innerHTML = (
        `<article class="notice-empty">` +
          `<strong>Sin mensajes configurados</strong>` +
        `</article>`
      );
      return;
    }

    const liveIds = new Set(state.voiceNotices.map((notice) => notice.id));
    state.noticeAccordionOpenIds = new Set(
      [...state.noticeAccordionOpenIds].filter((id) => liveIds.has(id))
    );
    if (!state.noticeAccordionInitialized && state.voiceNotices[0]) {
      state.noticeAccordionOpenIds = new Set([state.voiceNotices[0].id]);
      state.noticeAccordionInitialized = true;
    }

    els.voiceNoticesList.innerHTML = state.voiceNotices.map((notice, index) => {
      const isTimer = notice.trigger === "timer";
      const isAudio = notice.contentType === "audio";
      const audioLabel = notice.audioName || "Sin archivo seleccionado";
      const isOpen = state.noticeAccordionOpenIds.has(notice.id);
      const triggerOptions = NOTICE_TRIGGERS.map((item) => (
        `<option value="${escapeHtml(item.value)}"${item.value === notice.trigger ? " selected" : ""}>${escapeHtml(item.label)}</option>`
      )).join("");
      const contentOptions = NOTICE_CONTENT_TYPES.map((item) => (
        `<option value="${escapeHtml(item.value)}"${item.value === notice.contentType ? " selected" : ""}>${escapeHtml(item.label)}</option>`
      )).join("");

      return (
        `<details class="notice-accordion"${isOpen ? " open" : ""} data-notice-id="${escapeHtml(notice.id)}" data-notice-index="${index}">` +
          `<summary class="notice-summary">` +
            `<div class="notice-summary-main">` +
              `<strong class="notice-summary-title">${escapeHtml(noticeTriggerLabel(notice.trigger))}</strong>` +
              `<span class="notice-summary-subtitle">${escapeHtml(noticeSubtitle(notice))}</span>` +
            `</div>` +
            `<div class="notice-summary-meta">` +
              `<span class="notice-pill">${escapeHtml(noticeTypeLabel(notice))}</span>` +
              `${isTimer ? `<span class="notice-pill">${escapeHtml(`${notice.seconds}s`)}</span>` : ""}` +
              `<span class="notice-order-chip">Aviso ${String(index + 1).padStart(2, "0")}</span>` +
            `</div>` +
          `</summary>` +
          `<div class="notice-card-body">` +
            `<div class="notice-body-toolbar">` +
              `<span class="status-label">Edita el mensaje y colapsa este panel cuando quieras.</span>` +
              `<button class="secondary-button notice-remove-button" type="button" data-notice-remove="${index}">Eliminar</button>` +
            `</div>` +
            `<div class="notice-grid${isTimer ? " notice-grid-timer" : ""}">` +
              `<label class="field">` +
                `<span>Disparador</span>` +
                `<select data-notice-field="trigger">${triggerOptions}</select>` +
              `</label>` +
              `<label class="field">` +
                `<span>Tipo</span>` +
                `<select data-notice-field="contentType">${contentOptions}</select>` +
              `</label>` +
              `${isTimer ? (
                `<label class="field">` +
                  `<span>Tiempo (seg)</span>` +
                  `<input data-notice-field="seconds" type="number" min="5" step="5" value="${escapeHtml(notice.seconds)}">` +
                `</label>`
              ) : ""}` +
            `</div>` +
            `${isAudio ? (
              `<div class="notice-audio-shell">` +
                `<div class="notice-audio-row">` +
                  `<input class="notice-audio-name" type="text" value="${escapeHtml(audioLabel)}" readonly>` +
                  `<label class="secondary-button notice-file-button" type="button">` +
                    `Subir audio` +
                    `<input data-notice-audio="${index}" type="file" accept=".mp3,.m4a,.ogg,.aac,.wav,audio/mpeg,audio/mp4,audio/ogg,audio/aac,audio/wav">` +
                  `</label>` +
                `</div>` +
                `<span class="status-label">MP3, M4A, OGG, AAC o WAV ligero.</span>` +
              `</div>`
            ) : (
              `<label class="field">` +
                `<span>Mensaje</span>` +
                `<textarea class="notice-textarea" data-notice-field="message" rows="3" placeholder="Escribe el mensaje de este aviso.">${escapeHtml(notice.message)}</textarea>` +
              `</label>`
            )}` +
          `</div>` +
        `</details>`
      );
    }).join("");
  }

  function syncVoiceNoticesToLegacyFields() {
    els.voiceReadGifts.checked = false;
    els.voiceReadFollows.checked = false;
    els.voiceReadLikes.checked = false;
    els.voiceReadSubscribers.checked = false;
    els.voiceReadShares.checked = false;
    els.voiceReadChat.checked = !!els.voiceReadChatVisible?.checked;

    els.messageGiftTemplate.value = "";
    els.messageFollowTemplate.value = "";
    els.messageLikeTemplate.value = "";
    els.messageSubscriberTemplate.value = "";
    els.messageShareTemplate.value = "";
    els.messagePeriodicList.value = "";

    if (els.chatReadingScopeVisible && els.chatReadingScope) {
      els.chatReadingScope.value = els.chatReadingScopeVisible.value;
    }

    const periodicMessages = [];
    let periodicIntervalMs = 60000;
    let timerIntervalCaptured = false;

    state.voiceNotices.forEach((notice) => {
      const textValue = notice.contentType === "text" ? notice.message.trim() : "";
      if (notice.trigger === "gift" && textValue && !els.messageGiftTemplate.value) {
        els.voiceReadGifts.checked = true;
        els.messageGiftTemplate.value = textValue;
      }
      if (notice.trigger === "follow" && textValue && !els.messageFollowTemplate.value) {
        els.voiceReadFollows.checked = true;
        els.messageFollowTemplate.value = textValue;
      }
      if (notice.trigger === "like" && textValue && !els.messageLikeTemplate.value) {
        els.voiceReadLikes.checked = true;
        els.messageLikeTemplate.value = textValue;
      }
      if (notice.trigger === "subscription" && textValue && !els.messageSubscriberTemplate.value) {
        els.voiceReadSubscribers.checked = true;
        els.messageSubscriberTemplate.value = textValue;
      }
      if (notice.trigger === "share" && textValue && !els.messageShareTemplate.value) {
        els.voiceReadShares.checked = true;
        els.messageShareTemplate.value = textValue;
      }
      if (notice.trigger === "timer" && textValue) {
        periodicMessages.push(textValue);
        if (!timerIntervalCaptured) {
          periodicIntervalMs = Math.max(5000, (Number.parseInt(notice.seconds, 10) || 30) * 1000);
          timerIntervalCaptured = true;
        }
      }
    });

    els.messagePeriodicList.value = periodicMessages.join("\n");
    els.voicePeriodicInterval.value = normalizedPeriodicIntervalValue(periodicIntervalMs);
    updateChatReadingVisibility();
  }

  function seedVoiceNoticesFromHost(host) {
    if (state.voiceNoticesHydrated) {
      return;
    }

    if (!state.voiceNotices.length) {
      const seeded = [];
      if (host.giftThanksEnabled && host.giftThanksTemplate) {
        seeded.push(createVoiceNotice({ trigger: "gift", contentType: "text", message: host.giftThanksTemplate }));
      }
      if (host.followThanksEnabled && host.followThanksTemplate) {
        seeded.push(createVoiceNotice({ trigger: "follow", contentType: "text", message: host.followThanksTemplate }));
      }
      if (host.likeThanksEnabled && host.likeThanksTemplate) {
        seeded.push(createVoiceNotice({ trigger: "like", contentType: "text", message: host.likeThanksTemplate }));
      }
      if (host.subscriberThanksEnabled && host.subscriberThanksTemplate) {
        seeded.push(createVoiceNotice({ trigger: "subscription", contentType: "text", message: host.subscriberThanksTemplate }));
      }
      if (host.shareThanksEnabled && host.shareThanksTemplate) {
        seeded.push(createVoiceNotice({ trigger: "share", contentType: "text", message: host.shareThanksTemplate }));
      }
      (host.periodicMessages || []).forEach((message) => {
        if (String(message || "").trim()) {
          seeded.push(createVoiceNotice({
            trigger: "timer",
            contentType: "text",
            message: String(message || "").trim(),
            seconds: Math.max(30, Math.round((Number(host.periodicIntervalMs || 60000) || 60000) / 1000)),
          }));
        }
      });
      ["gift", "share", "follow", "like", "timer"].forEach((trigger) => {
        if (!seeded.some((notice) => notice.trigger === trigger)) {
          seeded.push(createVoiceNotice({
            trigger,
            contentType: "text",
            message: "",
            seconds: trigger === "timer" ? 60 : 30,
          }));
        }
      });
      state.voiceNotices = seeded;
      saveVoiceNotices();
    }

    state.voiceNoticesHydrated = true;
    renderVoiceNotices();
    syncVoiceNoticesToLegacyFields();
    updateTemplateSummary();
  }

  function periodicMessagesList() {
    return String(els.messagePeriodicList?.value || "")
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter(Boolean);
  }

  function apiGetJson(url) {
    return fetch(url, { cache: "no-store" }).then(async (response) => {
      const payload = await response.json().catch(() => ({ ok: false, message: `HTTP ${response.status}` }));
      if (payload && typeof payload === "object") {
        payload.__httpStatus = response.status;
      }
      return payload;
    });
  }

  function apiPostJson(url, payload) {
    return fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json; charset=utf-8" },
      body: JSON.stringify(payload ?? {}),
    }).then(async (response) => {
      const result = await response.json().catch(() => ({ ok: false, message: `HTTP ${response.status}` }));
      if (result && typeof result === "object") {
        result.__httpStatus = response.status;
      }
      return result;
    });
  }

  function apiPostText(url, text) {
    return fetch(url, {
      method: "POST",
      headers: { "Content-Type": "text/plain; charset=utf-8" },
      body: text,
    }).then(async (response) => {
      const result = await response.json().catch(() => ({ ok: false, message: `HTTP ${response.status}` }));
      if (result && typeof result === "object") {
        result.__httpStatus = response.status;
      }
      return result;
    });
  }

  async function handleAuthRequiredResponse(response, message = "") {
    if (!responseNeedsAuth(response)) {
      return false;
    }
    setAuthFeedback(
      message || "Valida usuario, contrase\u00f1a y licencia para continuar usando el panel.",
      "warn"
    );
    await loadState(true);
    appendLog("El panel qued\u00f3 bloqueado hasta validar el acceso remoto.");
    return true;
  }

  async function runCommand(command, options = {}) {
    if (!options.silent) {
      appendLog(`> ${command}`);
    }
    const response = await apiPostText("/api/command", command);
    if (await handleAuthRequiredResponse(response)) {
      return response;
    }
    const message =
      response.output
      || response.message
      || response.error
      || (response.ok ? "Hecho." : "No se pudo completar la acci\u00f3n.");
    if (!options.silent) {
      appendLog(message);
    }
    await refreshAll(true);
    return response;
  }

  async function postJsonAction(url, payload, label) {
    appendLog(`> ${label}`);
    const response = await apiPostJson(url, payload);
    if (await handleAuthRequiredResponse(response)) {
      return response;
    }
    const message =
      response.output
      || response.message
      || response.error
      || (response.ok ? "Hecho." : "No se pudo completar la acci\u00f3n.");
    appendLog(message);
    await refreshAll(true);
    return response;
  }

  function touchVoiceNoticeState(options = {}) {
    const rerender = options.rerender !== false;
    state.voiceDirty = true;
    saveVoiceNotices();
    syncVoiceNoticesToLegacyFields();
    if (rerender) {
      renderVoiceNotices();
    }
    updateTemplateSummary();
    renderVoiceSaveState();
  }

  function updateVoiceNoticeField(index, field, value, options = {}) {
    const notice = state.voiceNotices[index];
    if (!notice) {
      return;
    }
    if (field === "seconds") {
      notice.seconds = Math.max(5, Number.parseInt(value, 10) || 30);
    } else if (field === "trigger") {
      notice.trigger = NOTICE_TRIGGERS.some((item) => item.value === value) ? value : "gift";
    } else if (field === "contentType") {
      notice.contentType = NOTICE_CONTENT_TYPES.some((item) => item.value === value) ? value : "text";
    } else if (field === "message") {
      notice.message = String(value || "");
    }
    touchVoiceNoticeState(options);
  }

  async function attachVoiceNoticeAudio(index, file) {
    if (!file) {
      return;
    }
    if (file.size > AUDIO_NOTICE_MAX_BYTES) {
      appendLog("El audio del aviso supera el l\u00edmite ligero de 1.5 MB. Usa un MP3 u OGG m\u00e1s liviano.");
      return;
    }

    const notice = state.voiceNotices[index];
    if (!notice) {
      return;
    }

    const dataUrl = await new Promise((resolve, reject) => {
      const reader = new FileReader();
      reader.onload = () => resolve(typeof reader.result === "string" ? reader.result : "");
      reader.onerror = () => reject(new Error("No se pudo leer el archivo de audio."));
      reader.readAsDataURL(file);
    });

    notice.contentType = "audio";
    notice.audioName = file.name;
    notice.audioMimeType = file.type || "";
    notice.audioDataUrl = dataUrl;
    touchVoiceNoticeState();
  }

  function composeVoicePayload(extra = {}) {
    syncVoiceNoticesToLegacyFields();
    const periodicMessages = periodicMessagesList();

    return {
      action: extra.action || "",
      message: extra.message || "",
      ttsEnabled: els.voiceEnabled.checked,
      voiceId: els.voiceProfile.value,
      voiceLanguage: els.voiceLanguage.value,
      voiceFrequency: els.voiceFrequency.value,
      energyLevel: "balanced",
      toneStyle: toneForVoiceProfile(els.voiceProfile.value),
      allowChatMessages: els.voiceEnabled.checked && els.voiceReadChat.checked,
      chatFilterMode: els.chatReadingScope.value,
      chatMessageTemplate: els.messageChatTemplate.value.trim(),
      giftThanksEnabled: els.voiceEnabled.checked && els.voiceReadGifts.checked,
      followThanksEnabled: els.voiceEnabled.checked && els.voiceReadFollows.checked,
      likeThanksEnabled: els.voiceEnabled.checked && els.voiceReadLikes.checked,
      subscriberThanksEnabled: els.voiceEnabled.checked && els.voiceReadSubscribers.checked,
      shareThanksEnabled: els.voiceEnabled.checked && els.voiceReadShares.checked,
      periodicEnabled: els.voiceEnabled.checked && periodicMessages.length > 0,
      periodicIntervalMs: Number.parseInt(els.voicePeriodicInterval.value, 10) || 60000,
      replacePeriodicMessages: false,
      giftThanksTemplate: els.messageGiftTemplate.value.trim(),
      followThanksTemplate: els.messageFollowTemplate.value.trim(),
      likeThanksTemplate: els.messageLikeTemplate.value.trim(),
      subscriberThanksTemplate: els.messageSubscriberTemplate.value.trim(),
      shareThanksTemplate: els.messageShareTemplate.value.trim(),
      periodicMessages,
    };
  }

  function renderVoiceCatalog(host) {
    const catalog = Array.isArray(host?.voiceCatalog) ? host.voiceCatalog : [];
    if (!catalog.length) {
      return;
    }
    const currentValue = host.voiceId || els.voiceProfile.value || state.uiPrefs.voiceProfile;
    els.voiceProfile.innerHTML = catalog.map((voice) => {
      const label = voice.available
        ? localizedVoiceDisplayName(voice)
        : `${localizedVoiceDisplayName(voice)} (instalar)`;
      return `<option value="${escapeHtml(voice.id)}">${escapeHtml(label)}</option>`;
    }).join("");
    els.voiceProfile.value = currentValue;
    if (!els.voiceProfile.value && catalog[0]) {
      els.voiceProfile.value = catalog[0].id;
    }
  }

  function activityLabel(entry) {
    const raw = String(entry?.label || "");
    if (raw === "chat_message") return "Mensaje";
    if (raw === "gift") return "Regalo";
    if (raw === "like") return "Like";
    if (raw === "follow") return "Nuevo seguidor";
    if (raw === "viewer_join") return "Se uni\u00f3";
    if (raw === "share") return "Compartido";
    return raw ? raw.replaceAll("_", " ") : "Actualizaci\u00f3n";
  }

  function activityMonitorTag(entry) {
    const raw = String(entry?.label || "").toLowerCase();
    if (raw === "chat_message") return "CHAT";
    if (raw === "gift") return "GIFT";
    if (raw === "like") return "LIKE";
    if (raw === "follow") return "FOLLOW";
    if (raw === "viewer_join") return "JOIN";
    if (raw === "share") return "SHARE";
    return raw ? raw.replaceAll("_", " ").toUpperCase() : "EVENTO";
  }

  function activityMonitorTone(entry) {
    const raw = String(entry?.label || "").toLowerCase();
    if (raw === "gift") return "gift";
    if (raw === "share") return "share";
    if (raw === "follow" || raw === "viewer_join") return "follow";
    return "message";
  }

  function isCommunityActivity(entry) {
    const kind = String(entry?.kind || "").toLowerCase();
    const label = String(entry?.label || "").toLowerCase();
    const source = String(entry?.source || "").toLowerCase();
    return kind === "host_event"
      && COMMUNITY_ACTIVITY_LABELS.has(label)
      && source !== "panel_ui";
  }

  function recentActivityItems(limit = MAX_RECENT_ITEMS) {
    const items = state.eventsPayload.items?.length
      ? state.eventsPayload.items
      : state.payload?.snapshot?.recentActivity || [];
    const clearBeforeMs = resolveTimestampMs(state.activityClearBeforeMs);
    return items
      .map((entry) => ({
        ...entry,
        timestampMs: resolveTimestampMs(entry?.timestampMs),
      }))
      .filter((entry) => (
        entry.timestampMs > 0
          ? entry.timestampMs >= clearBeforeMs
          : clearBeforeMs === 0
      ))
      .filter((entry) => isCommunityActivity(entry))
      .slice(-limit);
  }

  function latestRecentActivityItem(limit = MAX_RECENT_ITEMS) {
    const items = recentActivityItems(limit);
    return items.length ? items[items.length - 1] : null;
  }

  function activitySummary(payload = state.payload) {
    const external = payload?.snapshot?.externalBridge || {};
    const auth = authSnapshot(payload);
    const connected = external.connectionState === "connected" || !!external.runnerRunning;
    const targetUser = external.targetUser ? `@${external.targetUser}` : "";
    const diagnosticsOk = payload?.diagnostics?.ok !== false;
    if (auth.required && !auth.authenticated) {
      return {
        tone: "danger",
        label: "Bloqueado",
        copy: "Valida tu acceso antes de usar el panel.",
      };
    }
    if (!diagnosticsOk && !connected) {
      return {
        tone: "danger",
        label: "Revisar",
        copy: targetUser
          ? `No se pudo completar la conexión con ${targetUser}.`
          : "Hay un problema que impide la conexión.",
      };
    }
    if (connected) {
      return {
        tone: "live",
        label: "En vivo",
        copy: targetUser
          ? `Sistema listo y conectado a ${targetUser}.`
          : "Sistema listo y escuchando actividad.",
      };
    }
    return {
      tone: "warn",
      label: "Preparado",
      copy: targetUser
        ? `${targetUser} est\u00e1 listo para conectar.`
        : "Sistema listo para conectar una cuenta de TikTok.",
    };
  }

  function renderAuth(payload) {
    const auth = authSnapshot(payload);
    const required = !!auth.required;
    const authenticated = !!auth.authenticated;
    const locked = required && !authenticated;
    const stateLabel = authenticated ? "Validado" : (required ? "Bloqueado" : "Modo local");
    const stateTone = authenticated ? "live" : (required ? "danger" : "warn");
    const serverMessage = auth.message
      || (authenticated
        ? "Acceso validado correctamente."
        : (required
          ? "Ingresa tus credenciales para habilitar este panel."
          : "La validaci\u00f3n remota no es obligatoria en esta configuraci\u00f3n."));
    const feedbackText = state.authUi.feedback || serverMessage;
    const feedbackTone = state.authUi.feedback
      ? state.authUi.feedbackTone
      : (authenticated ? "success" : (required ? "warn" : "warn"));

    setText(els.authStatePill, stateLabel);
    els.authStatePill?.classList.remove("is-live", "is-warn", "is-danger");
    els.authStatePill?.classList.add(`is-${stateTone}`);

    setText(els.authServerMessage, serverMessage);
    setText(els.authAccountLabel, auth.email || (authenticated ? "Cuenta validada" : "Sin validar"));
    setText(
      els.authLicenseLabel,
      auth.licenseKey || (authenticated ? "Licencia activa" : "Pendiente")
    );
    setText(els.authValidatedAt, formatDateTime(auth.lastValidatedTimestampMs));
    if (els.authMetaGrid) {
      const hasAuthMeta = authenticated
        || !!String(auth.email || "").trim()
        || !!String(auth.licenseKey || "").trim()
        || !!auth.lastValidatedTimestampMs;
      els.authMetaGrid.hidden = !hasAuthMeta;
    }

    if (els.authFeedback) {
      els.authFeedback.classList.remove("is-success", "is-error", "is-warn");
      els.authFeedback.classList.add(
        feedbackTone === "error"
          ? "is-error"
          : (feedbackTone === "success" ? "is-success" : "is-warn")
      );
      setText(els.authFeedback, feedbackText);
    }

    if (auth.email || auth.licenseKey) {
      syncAuthDraft({
        email: auth.email || state.authDraft.email,
        licenseKey: auth.licenseKey || state.authDraft.licenseKey,
      });
    }

    if (els.authEmail && !String(els.authEmail.value || "").trim()) {
      els.authEmail.value = auth.email || state.authDraft.email || "";
    }
    if (els.authPassword && !String(els.authPassword.value || "")) {
      els.authPassword.value = state.authDraft.password || "";
    }
    if (els.authLicenseKey && !String(els.authLicenseKey.value || "").trim()) {
      els.authLicenseKey.value = auth.licenseKey || state.authDraft.licenseKey || "";
    }

    if (els.authSubmitButton) {
      els.authSubmitButton.disabled = state.authUi.busy;
      setText(els.authSubmitButton, state.authUi.busy ? "Validando..." : "Validar acceso");
    }
    if (els.authSupportButton) {
      els.authSupportButton.disabled = state.authUi.busy;
    }

    if (els.authSessionRow) {
      els.authSessionRow.hidden = !required;
    }
    if (els.authSessionNote) {
      const sessionText = authenticated
        ? `Validado para ${auth.email || "esta cuenta"}`
        : (required
          ? "Bloqueado hasta validar licencia"
          : "Validaci\u00f3n remota desactivada");
      setText(els.authSessionNote, sessionText);
    }
    if (els.assistantAccessState) {
      setText(
        els.assistantAccessState,
        authenticated
          ? (auth.email || "Validado")
          : (required ? "Bloqueado" : "Modo local")
      );
    }
    if (els.authLogoutButton) {
      els.authLogoutButton.hidden = !(required && authenticated);
      els.authLogoutButton.disabled = state.authUi.busy;
    }

    document.body.classList.toggle("auth-locked", locked);
    document.documentElement.classList.toggle("auth-locked", locked);
    if (els.authGate) {
      els.authGate.hidden = !locked;
    }

    if (!locked) {
      clearAuthFeedback();
      return;
    }

    syncViewportMetrics();
    if (!authDialogHasActiveInput()) {
      window.requestAnimationFrame(focusAuthField);
    }
  }

  function renderConnection(payload) {
    const external = payload?.snapshot?.externalBridge || {};
    const diagnosticsOk = !!payload?.diagnostics?.ok;
    const connected = external.connectionState === "connected" || !!external.runnerRunning;
    const runtimeMissing = !!external.runtimeChecked && external.runtimeReady === false;
    const targetUser = external.targetUser ? `@${external.targetUser}` : "";
    const runnerIssue = humanizeRunnerIssue(external);
    let label = "Sin cuenta conectada";
    let meta = "Bridge en espera";
    let tone = diagnosticsOk ? "warn" : "danger";

    if (connected) {
      label = "Conectado";
      meta = targetUser || "Live en curso";
      tone = "live";
    } else if (runtimeMissing) {
      label = "Revisar bridge";
      meta = "Faltan dependencias";
      tone = "danger";
    } else if (targetUser) {
      label = "Listo para conectar";
      meta = targetUser;
      tone = diagnosticsOk ? "warn" : "danger";
    } else if (!diagnosticsOk && runnerIssue) {
      label = "Sin conexi\u00f3n";
      meta = "Requiere revisi\u00f3n";
      tone = "danger";
    }

    const note = connected
      ? (external.lastStatusMessage || "Conexi\u00f3n activa y escuchando eventos del live.")
      : (runnerIssue || external.lastStatusMessage || "Ingresa un usuario y presiona Conectar.");

    setText(els.connectionStatusText, label);
    setText(els.connectionStatusMeta, compactText(runtimeMissing ? (runnerIssue || external.runtimeSummary || meta) : meta, meta));
    setText(els.connectionTargetUser, targetUser || "Sin cuenta conectada");
    setText(els.liveRoom, external.currentRoomId || "Sala no disponible");
    setText(els.connectionNote, note);

    if (document.activeElement !== els.tiktokUser) {
      const desiredValue = external.targetUser ? `@${external.targetUser}` : els.tiktokUser.value;
      if (desiredValue !== els.tiktokUser.value) {
        els.tiktokUser.value = desiredValue;
      }
    }

    els.disconnectButton.hidden = !connected;
    els.connectButton.hidden = connected;
    setPillTone(els.connectionPill, tone);
  }

  function renderStreamMetrics(payload, metricsPayload) {
    const metrics = metricsPayload || payload?.metrics || {};
    const session = metrics.hostSession || null;
    const external = payload?.snapshot?.externalBridge || {};
    const latestItem = latestRecentActivityItem(18);
    const connected = external.connectionState === "connected" || !!external.runnerRunning;
    const liveViewerCount = Math.max(0, Number(session?.lastEvent?.viewerCount || session?.viewerCount || 0));
    const activePlayers = Math.max(0, Number(metrics.activePlayers || 0));
    let viewers = liveViewerCount || activePlayers || 0;
    if (connected && viewers > 0) {
      state.stickyMetrics.viewers = viewers;
    } else if (connected) {
      viewers = state.stickyMetrics.viewers;
    } else {
      state.stickyMetrics.viewers = 0;
    }
    const gifts = session ? Number(session.gifts || 0) : Number(external.giftEvents || 0);
    const likes = session ? Number(session.likes || 0) : Number(external.likeEvents || 0);
    const messages = session ? Number(session.chatMessages || 0) : Number(external.chatEvents || 0);
    const followers = session ? Number(session.follows || 0) : Number(external.followEvents || 0);
    const shares = session ? Number(session.shares || 0) : Number(external.shareEvents || 0);
    const hasSignal = connected || viewers > 0 || likes > 0 || gifts > 0 || messages > 0 || followers > 0 || shares > 0;

    setText(els.metricViewers, displayMetric(viewers, hasSignal), { animate: true });
    setText(els.metricLikes, displayMetric(likes, hasSignal), { animate: true });
    setText(els.metricGifts, displayMetric(gifts, hasSignal), { animate: true });
    setText(els.metricMessages, displayMetric(messages, hasSignal), { animate: true });
    setText(els.metricFollowers, displayMetric(followers, hasSignal), { animate: true });
    setText(els.metricShares, displayMetric(shares, hasSignal), { animate: true });
    setText(
      els.streamUpdated,
      latestItem?.timestampMs
        ? formatTime(latestItem.timestampMs)
        : (external.lastEventTimestampMs
          ? formatTime(external.lastEventTimestampMs)
        : (connected ? "Escuchando actividad" : "Conecta para empezar")
        )
    );
  }

  function renderVoice(payload) {
    const host = payload?.host || {};
    if (!state.voiceDirty) {
      state.uiPrefs.voiceProfile = host.voiceId || state.uiPrefs.voiceProfile;
      state.uiPrefs.voiceLanguage = host.voiceLanguage || state.uiPrefs.voiceLanguage;
      state.uiPrefs.voiceFrequency = host.voiceFrequency || state.uiPrefs.voiceFrequency || "normal";
      state.uiPrefs.chatReadingScope = host.chatFilterMode || state.uiPrefs.chatReadingScope;
      state.uiPrefs.readChat = !!host.allowChatMessages;
      saveUiPrefs();

      renderVoiceCatalog(host);
      els.voiceEnabled.checked = host.ttsEnabled ?? true;
      els.voiceProfile.value = host.voiceId || state.uiPrefs.voiceProfile;
      els.voiceLanguage.value = state.uiPrefs.voiceLanguage;
      els.voiceFrequency.value = state.uiPrefs.voiceFrequency;
      els.voiceReadChatVisible.checked = !!host.allowChatMessages;
      els.voiceReadGifts.checked = !!host.giftThanksEnabled;
      els.voiceReadFollows.checked = !!host.followThanksEnabled;
      els.voiceReadLikes.checked = !!host.likeThanksEnabled;
      els.voiceReadSubscribers.checked = !!host.subscriberThanksEnabled;
      els.voiceReadShares.checked = !!host.shareThanksEnabled;
      els.voiceReadChat.checked = !!host.allowChatMessages;
      els.chatReadingScope.value = host.chatFilterMode || state.uiPrefs.chatReadingScope;
      els.chatReadingScopeVisible.value = host.chatFilterMode || state.uiPrefs.chatReadingScope;
      els.voicePeriodicInterval.value = normalizedPeriodicIntervalValue(host.periodicIntervalMs || 60000);
      els.messageGiftTemplate.value = host.giftThanksTemplate || "";
      els.messageChatTemplate.value = host.chatMessageTemplate || "{message}";
      els.messageFollowTemplate.value = host.followThanksTemplate || "";
      els.messageLikeTemplate.value = host.likeThanksTemplate || "";
      els.messageSubscriberTemplate.value = host.subscriberThanksTemplate || "";
      els.messageShareTemplate.value = host.shareThanksTemplate || "";
      els.messagePeriodicList.value = (host.periodicMessages || []).join("\n");
    }

    if (!state.voiceNoticesHydrated) {
      seedVoiceNoticesFromHost(host);
    }
    updateChatReadingVisibility();
    updateTemplateSummary();
    renderVoiceSaveState();
  }

  function resolveGamePresentation(item) {
    const manifest = item?.manifest || {};
    const override = GAME_PRESENTATION_OVERRIDES[item?.gameId] || {};
    return {
      title: override.title || manifest.displayName || item?.displayName || "Juego",
      description: override.description || manifest.description || "",
      previewLabel: override.previewLabel || gameArtLetters(override.title || manifest.displayName || item?.displayName || "Juego").toUpperCase(),
      previewClass: override.previewClass || "preview-generic",
      previewImage: override.previewImage || "",
      meta: Array.isArray(override.meta) && override.meta.length ? override.meta : [],
    };
  }

  function gameStatusBadge(item, active) {
    if (active) {
      return "Activo";
    }
    if (item.installState === "downloading") {
      return "Descargando";
    }
    if (item.installState === "verifying") {
      return "Verificando";
    }
    if (item.installState === "installing") {
      return "Instalando";
    }
    if (item.installState === "failed") {
      return "Error";
    }
    if (item.updateAvailable) {
      return "Actualización";
    }
    if (item.installed === false) {
      return "No instalado";
    }
    return "Instalado";
  }

  function gameActionState(item, active) {
    if (active) {
      return {
        label: "Activo",
        className: "secondary-button",
        action: "active",
        disabled: true,
      };
    }
    if (item.installState === "downloading" || item.installState === "verifying" || item.installState === "installing") {
      return {
        label: "Procesando...",
        className: "secondary-button",
        action: "downloading",
        disabled: true,
      };
    }
    if (item.installed === false) {
      return {
        label: "Descargar",
        className: "secondary-button",
        action: "download",
        disabled: false,
      };
    }
    if (item.updateAvailable) {
      return {
        label: "Actualizar",
        className: "secondary-button",
        action: "download",
        disabled: false,
      };
    }
    if (item.enabled === false) {
      return {
        label: "No disponible",
        className: "secondary-button",
        action: "disabled",
        disabled: true,
      };
    }
    return {
      label: "Iniciar juego",
      className: "primary-button",
      action: "launch",
      disabled: false,
    };
  }

  function gameArtLetters(title) {
    const words = String(title || "Game").split(/\s+/).filter(Boolean);
    return (words[0]?.[0] || "G") + (words[1]?.[0] || "");
  }

  function renderGames(payload) {
    const items = [...(payload?.catalog?.items || [])].filter((item) => !HIDDEN_GAME_IDS.has(item.gameId));
    const activeGameId = payload?.gameDetail?.activeGameId || "";

    items.sort((left, right) => {
      const leftActive = left.gameId === activeGameId ? 1 : 0;
      const rightActive = right.gameId === activeGameId ? 1 : 0;
      return rightActive - leftActive;
    });

    setText(els.gamesCount, `${items.length} ${items.length === 1 ? "juego" : "juegos"}`);

    if (!items.length) {
      setHtml(els.gamesList, `<div class="empty-state">No hay juegos disponibles ahora mismo.</div>`, "gamesMarkup");
      return;
    }

    const markup = items.map((item) => {
      const active = item.gameId === activeGameId;
      const presentation = resolveGamePresentation(item);
      const action = gameActionState(item, active);
      const metaTags = presentation.meta
        .slice(0, 2)
        .map((tag) => `<span class="game-meta-chip">${escapeHtml(tag)}</span>`)
        .join("");
      const thumbContent = presentation.previewImage
        ? `<img class="game-thumb-image" src="${presentation.previewImage}" alt="">`
        : `<div class="game-thumb-badge">${escapeHtml(presentation.previewLabel)}</div>`;
      return (
        `<article class="game-card game-showcase-card${active ? " is-active" : ""}">` +
          `<div class="game-thumb ${escapeHtml(presentation.previewClass)}${presentation.previewImage ? " has-image" : ""}" aria-hidden="true">` +
            `${thumbContent}` +
          `</div>` +
          `<div class="game-content">` +
            `<div class="game-head">` +
              `<div>` +
                `<h3 class="game-title">${escapeHtml(presentation.title)}</h3>` +
                `${presentation.description ? `<p class="game-description">${escapeHtml(presentation.description)}</p>` : ""}` +
                `${metaTags ? `<div class="game-meta-row">${metaTags}</div>` : ""}` +
              `</div>` +
              `<span class="game-badge${active ? " active" : ""}">${gameStatusBadge(item, active)}</span>` +
            `</div>` +
            `<div class="game-actions">` +
              `<button class="${action.className}" type="button" data-game-id="${escapeHtml(item.gameId)}" data-game-action="${escapeHtml(action.action)}"${action.disabled ? " disabled" : ""}>` +
                `${action.label}` +
              `</button>` +
            `</div>` +
          `</div>` +
        `</article>`
      );
    }).join("");
    setHtml(els.gamesList, markup, "gamesMarkup");
  }

  function renderExternalGame(payload) {
    const externalGame = payload?.snapshot?.externalGame || {};
    const active = !!externalGame.active;
    const bridgeRunning = !!externalGame.bridgeRunning;
    const gameRunning = !!externalGame.gameRunning;
    const ranking = Array.isArray(externalGame.ranking) ? externalGame.ranking : [];
    const leader = ranking[0];

    if (els.gameRuntimeNote) {
      if (!externalGame.discovered) {
        setText(els.gameRuntimeNote, "Sin juego disponible");
      } else if (!active) {
        setText(els.gameRuntimeNote, `${externalGame.displayName || "Juego"} listo para abrir`);
      } else if (leader) {
        setText(
          els.gameRuntimeNote,
          `${externalGame.displayName || "Juego"} ${bridgeRunning && gameRunning ? "en vivo" : "iniciando"} \u00b7 ${leader.playerName} ${leader.score}`
        );
      } else {
        setText(
          els.gameRuntimeNote,
          `${externalGame.displayName || "Juego"} ${bridgeRunning && gameRunning ? "en vivo" : "iniciando"}`
        );
      }
    }
  }

  function renderSystemStatus(payload, metricsPayload) {
    const external = payload?.snapshot?.externalBridge || {};
    const metrics = metricsPayload || payload?.metrics || {};
    const connected = external.connectionState === "connected" || !!external.runnerRunning;
    const latency = latencyPresentation(metrics.pipelineLatencyMs || 0, connected);

    setText(els.statusLatency, latency.text, { animate: true });
    if (els.titlebarLatencyPill) {
      els.titlebarLatencyPill.hidden = !!latency.hidden;
    }
    setText(
      els.statusLastEvent,
      external.lastEventTimestampMs ? formatTime(external.lastEventTimestampMs) : "Sin actividad"
    );
    els.statusLatency?.classList.remove("metric-live", "metric-warn", "metric-danger");
    els.statusLatency?.classList.add(`metric-${latency.tone}`);
    setPillTone(els.titlebarLatencyPill, latency.tone);
  }

  function renderStartupAssistant(payload, metricsPayload) {
    const summary = activitySummary(payload);
    const external = payload?.snapshot?.externalBridge || {};
    const auth = authSnapshot(payload);
    const connected = external.connectionState === "connected" || !!external.runnerRunning;
    const diagnosticsOk = payload?.diagnostics?.ok !== false;
    const targetUser = external.targetUser ? `@${external.targetUser}` : "";
    let liveState = "Esperando tu cuenta";
    if (auth.required && !auth.authenticated) {
      liveState = "Bloqueado";
    } else if (connected) {
      liveState = "Escuchando eventos del live";
    } else if (!diagnosticsOk) {
      liveState = "Revisar conexi\u00f3n";
    } else if (targetUser) {
      liveState = "Listo para conectar";
    }

    setText(els.assistantSummaryText, summary.label);
    setText(els.assistantSummaryCopy, summary.copy);
    els.assistantSummaryDot?.classList.remove("live", "warn", "danger");
    els.assistantSummaryDot?.classList.add(
      summary.tone === "live" ? "live" : (summary.tone === "warn" ? "warn" : "danger")
    );
    setPillTone(els.assistantSummaryPill, summary.tone);

    setText(
      els.assistantTiktokState,
      targetUser || "Sin cuenta conectada"
    );
    setText(els.assistantLiveState, liveState);
  }

  function renderRecentActivity(payload) {
    if (!els.recentActivityList) {
      return;
    }
    const items = recentActivityItems(18);
    const external = payload?.snapshot?.externalBridge || {};
    const latestItem = latestRecentActivityItem(18);
    setText(
      els.activityUpdated,
      latestItem ? "Actividad en curso" : "Esperando"
    );
    setText(els.activityFeedUser, external.targetUser ? `@${external.targetUser}` : "Sin cuenta conectada");
    setText(
      els.activityFeedLastType,
      latestItem ? activityLabel(latestItem) : "Sin eventos"
    );

    if (!items.length) {
      const emptyText = external.targetUser
        ? "Los mensajes y acciones de tu comunidad apareceran aqui."
        : "Conecta tu cuenta y aqui veras la actividad de tu comunidad.";
      setHtml(els.recentActivityList, `<div class="empty-state">${escapeHtml(emptyText)}</div>`, "recentActivityMarkup");
      return;
    }

    const markup = items.map((entry) => (
      `<div class="activity-monitor-line">` +
        `<span class="activity-monitor-label tone-${escapeHtml(activityMonitorTone(entry))}">[${escapeHtml(activityMonitorTag(entry))}]</span>` +
        `<span class="activity-monitor-actor">${escapeHtml(entry.actorName || "Nisoje Studio")}</span>` +
        `${entry.details ? `<span class="activity-monitor-detail">${escapeHtml(entry.details)}</span>` : ""}` +
      `</div>`
    )).join("");
    setHtml(els.recentActivityList, markup, "recentActivityMarkup");
  }

  function renderAll(payload) {
    state.payload = payload;
    renderAuth(payload);
    renderConnection(payload);
    if (!uiInteractionLocked()) {
      renderVoice(payload);
      renderGames(payload);
    }
    renderStreamMetrics(payload, state.metricsPayload || payload.metrics);
    renderRecentActivity(payload);
    renderExternalGame(payload);
    renderAdvancedLogs();
  }

  async function loadState(force = false) {
    if (state.busyState) {
      return;
    }
    state.busyState = true;
    try {
      const payload = await apiGetJson("/api/state");
      state.payload = payload;
      state.eventsPayload = payload?.events || state.eventsPayload;
      state.metricsPayload = payload?.metrics || state.metricsPayload;
      detectIssueTransitions(payload);
      renderAuth(payload);
      if (uiInteractionLocked() && !force) {
        return;
      }
      renderAll(payload);
      if (force) {
        renderRecentActivity(payload);
      }
    } catch (error) {
      appendLog(`No se pudo cargar el estado del panel: ${error}`);
      setPillTone(els.connectionPill, "danger");
      setPillTone(els.titlebarLatencyPill, "danger");
      setText(els.connectionStatusText, "Sin respuesta");
      setText(els.connectionStatusMeta, "Panel local no disponible");
      setText(els.statusLatency, "Sin señal");
    } finally {
      state.busyState = false;
    }
  }

  async function loadRealtime(force = false) {
    if (state.busyRealtime) {
      return;
    }
    state.busyRealtime = true;
    try {
      const realtimePayload = await apiGetJson("/api/realtime");
      const eventsPayload = realtimePayload?.events || { total: 0, items: [] };
      const metricsPayload = realtimePayload?.metrics || null;
      state.eventsPayload = eventsPayload;
      state.metricsPayload = metricsPayload;
      if (uiInteractionLocked() && !force) {
        return;
      }
      if (state.payload && !uiInteractionLocked()) {
        renderStreamMetrics(state.payload, metricsPayload);
        renderRecentActivity(state.payload);
      }
      if (force && state.payload && !uiInteractionLocked()) {
        renderRecentActivity(state.payload);
      }
    } catch (error) {
      appendLog(`No se pudieron actualizar los datos del live: ${error}`);
    } finally {
      state.busyRealtime = false;
    }
  }

  async function refreshAll(force = false) {
    await loadState(force);
    await loadRealtime(force);
  }

  function pageIsHidden() {
    return document.visibilityState === "hidden";
  }

  function currentStatePollMs() {
    return pageIsHidden() ? POLL_STATE_HIDDEN_MS : POLL_STATE_MS;
  }

  function currentRealtimePollMs() {
    return pageIsHidden() ? POLL_REALTIME_HIDDEN_MS : POLL_REALTIME_MS;
  }

  function cancelPollingLoops() {
    if (state.statePollTimerId) {
      window.clearTimeout(state.statePollTimerId);
      state.statePollTimerId = 0;
    }
    if (state.realtimePollTimerId) {
      window.clearTimeout(state.realtimePollTimerId);
      state.realtimePollTimerId = 0;
    }
  }

  function scheduleStatePoll(delayMs = currentStatePollMs()) {
    if (state.statePollTimerId) {
      window.clearTimeout(state.statePollTimerId);
    }
    state.statePollTimerId = window.setTimeout(async () => {
      state.statePollTimerId = 0;
      await loadState(false);
      scheduleStatePoll();
    }, delayMs);
  }

  function scheduleRealtimePoll(delayMs = currentRealtimePollMs()) {
    if (state.realtimePollTimerId) {
      window.clearTimeout(state.realtimePollTimerId);
    }
    state.realtimePollTimerId = window.setTimeout(async () => {
      state.realtimePollTimerId = 0;
      await loadRealtime(false);
      scheduleRealtimePoll();
    }, delayMs);
  }

  function restartPollingLoops(immediate = false) {
    scheduleStatePoll(immediate ? 0 : currentStatePollMs());
    scheduleRealtimePoll(immediate ? 0 : currentRealtimePollMs());
  }

  async function submitAuthLogin() {
    const email = String(els.authEmail?.value || "").trim();
    const password = String(els.authPassword?.value || "");
    const licenseKey = normalizeLicenseKey(els.authLicenseKey?.value);

    if (!email || !password || !licenseKey) {
      setAuthFeedback("Completa usuario, contrase\u00f1a y licencia antes de validar.", "warn");
      focusAuthField();
      return;
    }

    syncAuthDraft({ email, password, licenseKey });
    state.authUi.busy = true;
    renderAuth(state.payload);
    try {
      const response = await apiPostJson("/api/auth/login", {
        email,
        password,
        licenseKey,
        deviceName: buildAuthDeviceName(),
        deviceId: buildAuthDeviceId(),
      });

      if (response?.ok) {
        clearAuthFeedback();
        appendLog("Acceso validado. El panel ya puede usarse.");
      } else {
        setAuthFeedback(
          response?.message || "No se pudo validar el acceso remoto.",
          response?.errorCode === "missing_fields" ? "warn" : "error"
        );
        appendLog(response?.message || "No se pudo validar el acceso remoto.");
        await exportSupportBundle("auth_login_failed", { silent: true });
      }

      await refreshAll(true);
    } catch (error) {
      setAuthFeedback(`No se pudo contactar el servidor de acceso: ${error}`, "error");
      appendLog(`No se pudo validar el acceso: ${error}`);
      await exportSupportBundle("auth_login_unreachable", { silent: true });
    } finally {
      state.authUi.busy = false;
      renderAuth(state.payload);
    }
  }

  async function logoutAccess() {
    state.authUi.busy = true;
    renderAuth(state.payload);
    try {
      await apiPostJson("/api/auth/logout", {});
      state.activityClearBeforeMs = 0;
      state.eventsPayload = { total: 0, items: [] };
      setAuthFeedback("El acceso fue cerrado en este equipo. Vuelve a validar para continuar.", "warn");
      appendLog("La sesi\u00f3n remota fue cerrada en este equipo.");
      await refreshAll(true);
    } catch (error) {
      setAuthFeedback(`No se pudo cerrar la sesi\u00f3n remota: ${error}`, "error");
      appendLog(`No se pudo cerrar la sesi\u00f3n remota: ${error}`);
    } finally {
      state.authUi.busy = false;
      renderAuth(state.payload);
    }
  }

  async function connectLive() {
    const user = normalizedUser(els.tiktokUser.value);
    if (!user) {
      appendLog("Escribe primero el usuario de TikTok.");
      els.tiktokUser.focus();
      return;
    }

    try {
      const attachResponse = await runCommand(`bridge attach ${user}`);
      if (!attachResponse?.ok) {
        await exportSupportBundle("tiktok_attach_failed", { silent: true });
        return;
      }

      const saveResponse = await runCommand("config save", { silent: true });
      if (saveResponse && saveResponse.ok === false) {
        appendLog("No se pudo guardar el usuario de TikTok en la configuraci\u00f3n actual.");
      }

      const runnerResponse = await runCommand(`bridge runner start ${user}`);
      if (!runnerResponse?.ok) {
        await exportSupportBundle("tiktok_runner_start_failed", { silent: true });
      }
    } catch (error) {
      appendLog(`No se pudo conectar: ${error}`);
      await exportSupportBundle("tiktok_runner_launch_error", { silent: true });
    }
  }

  async function disconnectLive() {
    try {
      await runCommand("bridge runner stop");
    } catch (error) {
      appendLog(`No se pudo desconectar: ${error}`);
    }
  }

  async function triggerExternalGameEvent(payload, label) {
    const externalGame = state.payload?.snapshot?.externalGame || {};
    if (!externalGame.active || !externalGame.bridgeRunning) {
      appendLog("Activa primero Arena Live para enviar eventos manuales.");
      return;
    }

    try {
      await postJsonAction("/api/game/trigger", payload, label);
    } catch (error) {
      appendLog(`No se pudo enviar ${label}: ${error}`);
    }
  }

  async function triggerActivityEvent(payload, label) {
    try {
      await postJsonAction("/api/game/trigger", payload, label);
    } catch (error) {
      appendLog(`No se pudo completar ${label}: ${error}`);
    }
  }

  async function saveVoiceSettings(extra = {}) {
    try {
      const response = await postJsonAction(
        "/api/host/tts",
        composeVoicePayload(extra),
        extra.action === "speak_now" ? "probar voz" : "guardar ajustes de voz"
      );
      if (response.ok) {
        state.voiceDirty = false;
        renderVoiceSaveState();
        await refreshAll(true);
      }
    } catch (error) {
      appendLog(`No se pudieron guardar los ajustes de voz: ${error}`);
    }
  }

  function bindEvents() {
    els.authForm?.addEventListener("submit", (event) => {
      event.preventDefault();
      submitAuthLogin();
    });

    [
      els.authEmail,
      els.authPassword,
      els.authLicenseKey,
    ].forEach((element) => {
      element?.addEventListener("input", () => {
        syncAuthDraft({
          email: String(els.authEmail?.value || "").trim(),
          password: String(els.authPassword?.value || ""),
          licenseKey: normalizeLicenseKey(els.authLicenseKey?.value),
        });
      });
    });

    els.authLicenseKey?.addEventListener("change", () => {
      els.authLicenseKey.value = normalizeLicenseKey(els.authLicenseKey.value);
      syncAuthDraft({
        licenseKey: els.authLicenseKey.value,
      });
    });

    els.authLogoutButton?.addEventListener("click", () => {
      logoutAccess();
    });

    els.authSupportButton?.addEventListener("click", () => {
      exportSupportBundle("auth_manual");
    });

    els.connectForm?.addEventListener("submit", (event) => {
      event.preventDefault();
      connectLive();
    });

    els.disconnectButton?.addEventListener("click", () => {
      disconnectLive();
    });

    els.voiceEnabled?.addEventListener("change", () => {
      holdUiInteraction();
      state.voiceDirty = true;
      updateChatReadingVisibility();
      updateTemplateSummary();
      renderVoiceSaveState();
    });

    els.voiceForm?.addEventListener("focusin", () => {
      holdUiInteraction(6000);
      state.voiceDirty = true;
      renderVoiceSaveState();
    });

    els.voiceForm?.addEventListener("pointerdown", () => {
      holdUiInteraction(6000);
    });

    els.voiceAddNoticeButton?.addEventListener("click", () => {
      holdUiInteraction();
      const notice = createVoiceNotice({ trigger: "gift", contentType: "text", seconds: 30 });
      state.voiceNotices.push(notice);
      state.noticeAccordionOpenIds.add(notice.id);
      state.noticeAccordionInitialized = true;
      touchVoiceNoticeState();
    });

    els.voiceNoticesList?.addEventListener("toggle", (event) => {
      const details = event.target.closest(".notice-accordion");
      if (!details || details.parentElement !== els.voiceNoticesList) {
        return;
      }
      const noticeId = details.dataset.noticeId || "";
      if (!noticeId) {
        return;
      }
      state.noticeAccordionInitialized = true;
      if (details.open) {
        state.noticeAccordionOpenIds.add(noticeId);
      } else {
        state.noticeAccordionOpenIds.delete(noticeId);
      }
    });

    els.voiceNoticesList?.addEventListener("click", (event) => {
      const removeButton = event.target.closest("[data-notice-remove]");
      if (!removeButton) {
        return;
      }
      const index = Number.parseInt(removeButton.dataset.noticeRemove || "", 10);
      if (!Number.isInteger(index)) {
        return;
      }
      holdUiInteraction(6000);
      const [removedNotice] = state.voiceNotices.splice(index, 1);
      if (removedNotice?.id) {
        state.noticeAccordionOpenIds.delete(removedNotice.id);
      }
      touchVoiceNoticeState();
    });

    els.voiceNoticesList?.addEventListener("input", (event) => {
      const target = event.target;
      const row = target.closest("[data-notice-index]");
      if (!row || !("dataset" in target)) {
        return;
      }
      const index = Number.parseInt(row.dataset.noticeIndex || "", 10);
      const field = target.dataset.noticeField || "";
      if (!Number.isInteger(index) || !field) {
        return;
      }
      holdUiInteraction(6000);
      updateVoiceNoticeField(index, field, target.value, { rerender: false });
    });

    els.voiceNoticesList?.addEventListener("change", async (event) => {
      const target = event.target;
      const row = target.closest("[data-notice-index]");
      if (!row) {
        return;
      }
      const index = Number.parseInt(row.dataset.noticeIndex || "", 10);
      if (!Number.isInteger(index)) {
        return;
      }
      holdUiInteraction(6000);
      if (target.dataset.noticeAudio) {
        try {
          await attachVoiceNoticeAudio(index, target.files?.[0] || null);
        } catch (error) {
          appendLog(`No se pudo adjuntar el audio del aviso: ${error}`);
        }
        return;
      }
      const field = target.dataset.noticeField || "";
      if (field) {
        updateVoiceNoticeField(index, field, target.value, { rerender: field !== "seconds" });
      }
    });

    els.voiceNoticesList?.addEventListener("pointerdown", () => {
      holdUiInteraction(6000);
    });

    els.voiceNoticesList?.addEventListener("focusin", () => {
      holdUiInteraction(6000);
    });

    els.voiceNoticesList?.addEventListener("keydown", () => {
      holdUiInteraction(6000);
    });

    [
      els.voiceReadGifts,
      els.voiceReadFollows,
      els.voiceReadLikes,
      els.voiceReadSubscribers,
      els.voiceReadShares,
      els.voiceReadChat,
    ].forEach((element) => {
      element?.addEventListener("change", () => {
        state.voiceDirty = true;
        updateChatReadingVisibility();
        updateTemplateSummary();
        renderVoiceSaveState();
      });
    });

    els.voiceReadChatVisible?.addEventListener("change", () => {
      holdUiInteraction();
      state.voiceDirty = true;
      state.uiPrefs.readChat = els.voiceReadChatVisible.checked;
      saveUiPrefs();
      updateChatReadingVisibility();
      updateTemplateSummary();
      renderVoiceSaveState();
    });

    els.chatReadingScopeVisible?.addEventListener("change", () => {
      holdUiInteraction();
      state.voiceDirty = true;
      state.uiPrefs.chatReadingScope = els.chatReadingScopeVisible.value;
      saveUiPrefs();
      updateChatReadingVisibility();
      updateTemplateSummary();
      renderVoiceSaveState();
    });

    [
      els.voiceProfile,
      els.voiceLanguage,
      els.voiceFrequency,
      els.chatReadingScope,
      els.voicePeriodicInterval,
      els.messageGiftTemplate,
      els.messageChatTemplate,
      els.messageFollowTemplate,
      els.messageLikeTemplate,
      els.messageSubscriberTemplate,
      els.messageShareTemplate,
      els.messagePeriodicList,
    ].forEach((element) => {
      element?.addEventListener("input", () => {
        holdUiInteraction();
        state.voiceDirty = true;
        updateTemplateSummary();
        renderVoiceSaveState();
      });
      element?.addEventListener("change", () => {
        holdUiInteraction();
        state.voiceDirty = true;
        updateTemplateSummary();
        renderVoiceSaveState();
      });
    });

    els.voiceProfile?.addEventListener("change", () => {
      state.uiPrefs.voiceProfile = els.voiceProfile.value;
      saveUiPrefs();
    });

    els.voiceLanguage?.addEventListener("change", () => {
      state.uiPrefs.voiceLanguage = els.voiceLanguage.value;
      saveUiPrefs();
    });

    els.voiceFrequency?.addEventListener("change", () => {
      state.uiPrefs.voiceFrequency = els.voiceFrequency.value;
      const interval = FREQUENCY_INTERVAL_MAP[els.voiceFrequency.value];
      if (interval) {
        els.voicePeriodicInterval.value = interval;
      }
      saveUiPrefs();
    });

    els.chatReadingScope?.addEventListener("change", () => {
      state.uiPrefs.chatReadingScope = els.chatReadingScope.value;
      saveUiPrefs();
    });

    els.voiceForm?.addEventListener("submit", (event) => {
      event.preventDefault();
      saveVoiceSettings();
    });

    els.voiceSpeakButton?.addEventListener("click", () => {
      postJsonAction("/api/tts/test", { message: "Prueba de voz de Nisoje Studio" }, "probar voz");
    });

    els.gamesList?.addEventListener("click", (event) => {
      const button = event.target.closest("button[data-game-id]");
      if (!button) {
        return;
      }
      const gameId = button.dataset.gameId || "";
      const action = button.dataset.gameAction || "launch";
      if (!gameId) {
        return;
      }
      if (action === "download") {
        postJsonAction("/api/game/download", { gameId }, `descargar juego ${gameId}`)
          .catch((error) => {
            appendLog(`No se pudo descargar ${gameId}: ${error}`);
          });
        return;
      }
      if (action === "downloading") {
        appendLog(`El juego ${gameId} ya se está procesando.`);
        return;
      }
      if (action === "disabled") {
        appendLog(`El juego ${gameId} no est\u00e1 disponible en este host.`);
        return;
      }
      postJsonAction("/api/game/start", { gameId }, `activar juego ${gameId}`)
        .then((response) => {
          if (response?.ok) {
            return runCommand("config save", { silent: true });
          }
          return null;
        })
        .catch((error) => {
          appendLog(`No se pudo guardar el juego seleccionado: ${error}`);
        });
    });

    els.refreshButton?.addEventListener("click", () => {
      refreshAll(true);
    });

    els.reconnectButton?.addEventListener("click", () => {
      postJsonAction("/api/system/reconnect", {}, "reconectar live");
    });

    els.metricsResetButton?.addEventListener("click", async () => {
      state.stickyMetrics.viewers = 0;
      await postJsonAction("/api/metrics/reset", {}, "reiniciar metricas");
      await loadRealtime(true);
    });

    els.supportExportButton?.addEventListener("click", () => {
      exportSupportBundle("manual");
    });

    els.activityClearButton?.addEventListener("click", () => {
      state.activityClearBeforeMs = Date.now();
      renderRecentActivity(state.payload);
    });

    els.activityGiftMenu?.addEventListener("click", (event) => {
      const button = event.target.closest("[data-gift-value]");
      if (!button) {
        return;
      }
      selectActivityGift(button.dataset.giftValue || ACTIVITY_GIFT_PRESETS[0].value);
    });

    els.activityChatButton?.addEventListener("click", () => {
      const message = String(els.activityChatInput?.value || "").trim();
      if (!message) {
        appendLog("Escribe un mensaje antes de enviarlo al monitor.");
        els.activityChatInput?.focus();
        return;
      }
      triggerActivityEvent({
        kind: "chat",
        actorId: "panel-chat-user",
        actorName: "Chat Pilot",
        avatarUrl: SAMPLE_AVATAR_DATA_URL,
        message,
        magnitude: 1,
      }, "enviar chat de prueba");
      if (els.activityChatInput) {
        els.activityChatInput.value = "";
      }
    });

    els.activityChatInput?.addEventListener("keydown", (event) => {
      if (event.key === "Enter") {
        event.preventDefault();
        els.activityChatButton?.click();
      }
    });

    els.activityGiftButton?.addEventListener("click", () => {
      const preset = activityGiftPreset();
      triggerActivityEvent({
        kind: "gift",
        actorId: "panel-gift-user",
        actorName: "Gift Pilot",
        avatarUrl: SAMPLE_AVATAR_DATA_URL,
        giftName: preset.name,
        quantity: 1,
        value: preset.coins,
      }, `enviar regalo ${preset.name.toLowerCase()}`);
    });

    els.activityShareButton?.addEventListener("click", () => {
      triggerActivityEvent({
        kind: "share",
        actorId: "panel-share-user",
        actorName: "Share Pilot",
        avatarUrl: SAMPLE_AVATAR_DATA_URL,
        message: "share",
      }, "enviar compartido de prueba");
    });

    els.activityFollowButton?.addEventListener("click", () => {
      triggerActivityEvent({
        kind: "follow",
        actorId: "panel-follow-user",
        actorName: "Follow Pilot",
        avatarUrl: SAMPLE_AVATAR_DATA_URL,
        message: "follow",
      }, "enviar seguidor de prueba");
    });

    els.activityLikeButton?.addEventListener("click", () => {
      const magnitude = Math.max(1, Number.parseInt(els.activityLikeCount?.value || "15", 10) || 15);
      if (els.activityLikeCount) {
        els.activityLikeCount.value = String(magnitude);
      }
      triggerActivityEvent({
        kind: "like",
        actorId: "panel-like-user",
        actorName: "Like Pilot",
        avatarUrl: SAMPLE_AVATAR_DATA_URL,
        magnitude,
      }, `enviar ${magnitude} likes de prueba`);
    });

    els.windowMinimizeButton?.addEventListener("click", () => {
      sendWindowAction("minimize");
    });

    els.windowMaximizeButton?.addEventListener("click", () => {
      sendWindowAction("toggle-maximize");
    });

    els.windowCloseButton?.addEventListener("click", () => {
      sendWindowAction("close");
    });

    els.titlebarDragRegion?.addEventListener("pointerdown", (event) => {
      const target = event.target;
      if (event.button !== 0 || target?.closest?.("button, input, select, textarea, a")) {
        return;
      }
      event.preventDefault();
      sendWindowAction("drag");
    });

    els.titlebarDragRegion?.addEventListener("dblclick", (event) => {
      const target = event.target;
      if (target?.closest?.("button, input, select, textarea, a")) {
        return;
      }
      sendWindowAction("toggle-maximize");
    });
  }

  function init() {
    syncViewportMetrics();
    updateTitlebarClock();
    renderAdvancedLogs();
    if (els.authEmail && state.authDraft.email) {
      els.authEmail.value = state.authDraft.email;
    }
    if (els.authPassword && state.authDraft.password) {
      els.authPassword.value = state.authDraft.password;
    }
    if (els.authLicenseKey && state.authDraft.licenseKey) {
      els.authLicenseKey.value = state.authDraft.licenseKey;
    }
    if (els.voiceReadChatVisible) {
      els.voiceReadChatVisible.checked = !!state.uiPrefs.readChat;
    }
    if (els.chatReadingScopeVisible) {
      els.chatReadingScopeVisible.value = state.uiPrefs.chatReadingScope;
    }
    bindEvents();
    renderActivityGiftMenu();
    if (!supportsHostWindowControls()) {
      [
        els.windowMinimizeButton,
        els.windowMaximizeButton,
        els.windowCloseButton,
      ].forEach((button) => {
        if (!button) return;
        button.disabled = true;
      });
    }
    window.addEventListener("resize", syncViewportMetrics);
    window.visualViewport?.addEventListener("resize", syncViewportMetrics);
    document.addEventListener("visibilitychange", () => {
      restartPollingLoops(!pageIsHidden());
    });
    window.addEventListener("beforeunload", cancelPollingLoops);
    updateChatReadingVisibility();
    updateTemplateSummary();
    refreshAll(true);
    restartPollingLoops(false);
    window.setInterval(updateTitlebarClock, 1000);
  }

  init();
})();
