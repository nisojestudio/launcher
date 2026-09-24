"""Catalogo unico de errores de conexion TikTok del bridge.

Fuente de verdad compartida por el bridge (clasificacion y politica de
reintentos) y por el panel (alertas visibles en el monitor del live).

Cada entrada define:
- ``code``: codigo canonico que viaja al panel.
- ``message``: mensaje en espanol listo para mostrar al usuario.
- ``severity``: ``info`` | ``warn`` | ``error`` para el monitor.
- ``action``: que deberia hacer el bridge/panel:
    * ``none``         -> no reintentar, requiere accion del usuario.
    * ``retry``        -> reintentar con backoff.
    * ``wait_for_live``-> la cuenta no esta en vivo: esperar a que empiece.
    * ``rotate_key``   -> cuota/limite del proveedor: usar otra API key.
    * ``fix_user``     -> el username no existe: corregirlo en el panel.
    * ``check_key``    -> credencial invalida: revisarla en el panel.
    * ``wait_provider``-> fallo temporal del proveedor: reintentar.
- ``retryable``: si el bucle de reconexion puede intentarlo otra vez.
"""

from __future__ import annotations

from dataclasses import dataclass

SEVERITY_INFO = "info"
SEVERITY_WARN = "warn"
SEVERITY_ERROR = "error"

ACTION_NONE = "none"
ACTION_RETRY = "retry"
ACTION_WAIT_FOR_LIVE = "wait_for_live"
ACTION_ROTATE_KEY = "rotate_key"
ACTION_FIX_USER = "fix_user"
ACTION_CHECK_KEY = "check_key"
ACTION_WAIT_PROVIDER = "wait_provider"


@dataclass(frozen=True, slots=True)
class ErrorSpec:
    code: str
    message: str
    severity: str
    action: str
    retryable: bool


CATALOG: dict[str, ErrorSpec] = {
    "INVALID_USERNAME": ErrorSpec(
        "INVALID_USERNAME",
        "El usuario de TikTok no es valido. Revisa el @ y volve a intentar.",
        SEVERITY_WARN,
        ACTION_FIX_USER,
        False,
    ),
    "USER_NOT_FOUND": ErrorSpec(
        "USER_NOT_FOUND",
        "No se encontro ese usuario en TikTok. Revisa que el @ sea exacto.",
        SEVERITY_WARN,
        ACTION_FIX_USER,
        False,
    ),
    "NOT_LIVE": ErrorSpec(
        "NOT_LIVE",
        "La cuenta no esta en vivo todavia. El panel sigue intentando.",
        SEVERITY_INFO,
        ACTION_WAIT_FOR_LIVE,
        True,
    ),
    "WAITING_ROOM": ErrorSpec(
        "WAITING_ROOM",
        "El proveedor todavia no confirma la sala del live. El panel sigue esperando.",
        SEVERITY_INFO,
        ACTION_WAIT_FOR_LIVE,
        True,
    ),
    "API_SESSION_ENDED": ErrorSpec(
        "API_SESSION_ENDED",
        "El proveedor cerro la sesion de la API key: se agoto la cuota o vencio el plan.",
        SEVERITY_WARN,
        ACTION_ROTATE_KEY,
        True,
    ),
    "RATE_LIMIT": ErrorSpec(
        "RATE_LIMIT",
        "El proveedor limito los intentos. Se reintenta en unos segundos.",
        SEVERITY_WARN,
        ACTION_WAIT_PROVIDER,
        True,
    ),
    "INVALID_API_KEY": ErrorSpec(
        "INVALID_API_KEY",
        "La API key del proveedor no es valida o esta vencida. Revisala en el panel.",
        SEVERITY_ERROR,
        ACTION_CHECK_KEY,
        False,
    ),
    "INVALID_JWT": ErrorSpec(
        "INVALID_JWT",
        "La API key de Euler Stream no es un JWT valido. Revisala en el panel.",
        SEVERITY_ERROR,
        ACTION_CHECK_KEY,
        False,
    ),
    "RELAY_ERROR": ErrorSpec(
        "RELAY_ERROR",
        "El proveedor reporto un error temporal de relay. Se reintenta.",
        SEVERITY_WARN,
        ACTION_WAIT_PROVIDER,
        True,
    ),
    "SERVER_RESTART": ErrorSpec(
        "SERVER_RESTART",
        "El proveedor se esta reiniciando. Se reconecta automaticamente.",
        SEVERITY_INFO,
        ACTION_WAIT_PROVIDER,
        True,
    ),
    "NETWORK_ERROR": ErrorSpec(
        "NETWORK_ERROR",
        "No hubo respuesta del proveedor. Se reintenta.",
        SEVERITY_WARN,
        ACTION_WAIT_PROVIDER,
        True,
    ),
    "STREAM_DISCONNECTED": ErrorSpec(
        "STREAM_DISCONNECTED",
        "La conexion con TikTok se cerro. Se reintenta.",
        SEVERITY_WARN,
        ACTION_RETRY,
        True,
    ),
    "AGE_RESTRICTED": ErrorSpec(
        "AGE_RESTRICTED",
        "El live requiere una sesion autenticada de TikTok.",
        SEVERITY_ERROR,
        ACTION_NONE,
        False,
    ),
    "ACCESS_BLOCKED": ErrorSpec(
        "ACCESS_BLOCKED",
        "TikTok bloqueo temporalmente el acceso automatico a ese live.",
        SEVERITY_ERROR,
        ACTION_NONE,
        False,
    ),
    "BOOTSTRAP_FAILED": ErrorSpec(
        "BOOTSTRAP_FAILED",
        "Falta una dependencia del bridge (websockets). Reinstala el panel.",
        SEVERITY_ERROR,
        ACTION_NONE,
        False,
    ),
    "CONNECT_TIMEOUT": ErrorSpec(
        "CONNECT_TIMEOUT",
        "TikTok no completo el handshake a tiempo. Se reintenta.",
        SEVERITY_WARN,
        ACTION_WAIT_PROVIDER,
        True,
    ),
    "UNKNOWN": ErrorSpec(
        "UNKNOWN",
        "No se pudo completar la conexion con TikTok.",
        SEVERITY_WARN,
        ACTION_RETRY,
        True,
    ),
}


# Codigos de cierre de tik.tools. Ver README_bridge.md.
_TIKTOOLS_CLOSE_CODE_MAP: dict[int, str] = {
    4005: "NOT_LIVE",
    4006: "NOT_LIVE",
    4404: "NOT_LIVE",          # Creator is not currently live
    4401: "API_SESSION_ENDED",  # Evaluation period ended (plan vencido)
    4429: "API_SESSION_ENDED",  # Demo/concurrent session limit
    4555: "API_SESSION_ENDED",  # Daily Demo Limit Reached
    4556: "RELAY_ERROR",        # Relay connection error
    4500: "NETWORK_ERROR",
    1012: "SERVER_RESTART",     # service restart
    1001: "STREAM_DISCONNECTED",
    1000: "STREAM_DISCONNECTED",
}

# Textos que el proveedor manda en el motivo de cierre. El orden importa:
# las condiciones de cuota van antes que los textos genericos de red.
_TEXT_RULES: tuple[tuple[tuple[str, ...], str], ...] = (
    (
        (
            "daily demo limit",
            "demo session ended",
            "upgrade required",
            "evaluation period",
            "pricing to continue",
            "quota",
        ),
        "API_SESSION_ENDED",
    ),
    (("concurrent websocket", "too many connections"), "API_SESSION_ENDED"),
    (("relay connection error",), "RELAY_ERROR"),
    (("service restart", "server restart"), "SERVER_RESTART"),
    (("invalid api key", "invalid api-key", "invalid key", "unauthorized", "forbidden"), "INVALID_API_KEY"),
    (("invalid unique_id", "invalid username", "invalid user", "bad username"), "INVALID_USERNAME"),
    (("user_not_found", "user not found", "usernotfound", "profile not found", "does not exist",
      "cannot find user", "no se encontro ese usuario"), "USER_NOT_FOUND"),
    (("not currently live", "is not live", "isn't live", "not live", "offline", "live has ended"), "NOT_LIVE"),
    (("age restricted", "sessionid", "session id"), "AGE_RESTRICTED"),
    (("detected by tiktok", "blocked by tiktok", "rejected by tiktok", "sign api", "sign server"), "ACCESS_BLOCKED"),
    (("rate limit", "rate_limit", "too many requests"), "RATE_LIMIT"),
    (("server rejected websocket", "http 502", "http 503", "bad gateway", "empty request"), "NETWORK_ERROR"),
    (("timed out", "timeout", "no close frame", "network", "connection", "websocket closed", "dns"),
     "NETWORK_ERROR"),
)


def spec_for(code: str) -> ErrorSpec:
    """Devuelve la especificacion del codigo, con fallback a UNKNOWN."""
    return CATALOG.get(str(code or "").strip().upper(), CATALOG["UNKNOWN"])


def is_retryable(code: str) -> bool:
    return spec_for(code).retryable


def action_for(code: str) -> str:
    return spec_for(code).action


def message_for(code: str) -> str:
    return spec_for(code).message


def severity_for(code: str) -> str:
    return spec_for(code).severity


def canonical_code(value: str) -> str:
    """Normaliza un codigo recibido (alias historicos) al canonico."""
    text = str(value or "").strip().upper()
    if text in CATALOG:
        return text
    if text in {"QUOTA_EXHAUSTED", "DEMO_LIMIT", "SESSION_LIMIT"}:
        return "API_SESSION_ENDED"
    if text in {"ACCESS_DENIED", "AUTH_FAILED"}:
        return "INVALID_API_KEY"
    if text in {"RELAY_CONNECTION_ERROR"}:
        return "RELAY_ERROR"
    if text in {"SERVER_RESTARTING"}:
        return "SERVER_RESTART"
    return "UNKNOWN"


def classify_close_code(close_code: int | None, reason: str = "") -> str:
    """Codigo canonico a partir del codigo de cierre del WebSocket."""
    if close_code is not None:
        mapped = _TIKTOOLS_CLOSE_CODE_MAP.get(int(close_code))
        if mapped is not None:
            return mapped
    text_code = classify_error_text(reason)
    if text_code != "UNKNOWN":
        return text_code
    return "STREAM_DISCONNECTED"


def classify_error_text(raw_error: str) -> str:
    """Codigo canonico a partir del texto de error del proveedor."""
    lower = str(raw_error or "").strip().lower()
    if not lower:
        return "UNKNOWN"
    for tokens, code in _TEXT_RULES:
        if any(token in lower for token in tokens):
            return code
    return "UNKNOWN"
