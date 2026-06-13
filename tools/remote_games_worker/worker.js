const DEFAULT_FIREBASE_API_KEY = "";
const DEFAULT_GAMES_CATALOG_OBJECT = "catalog/latest.json";
const DEFAULT_DOWNLOAD_TTL_SECONDS = 300;
const DEFAULT_GAME_DOWNLOAD_ROUTE = "/api/me/games/download";
const DEFAULT_ADMIN_EMAIL = "";
const DEFAULT_INSTALLER_URL = "";
const CORS_ALLOW_HEADERS = "authorization, content-type";
const CORS_ALLOW_METHODS = "GET, POST, OPTIONS";
let configuredCorsAllowOrigin = "";

const ADMIN_PROTECTED_PATHS = new Set([
  "/api/users",
  "/api/create-license",
  "/api/licenses",
  "/api/devices",
  "/api/devices-active",
  "/api/license/deactivate",
  "/api/admin/users",
  "/api/admin/licenses",
  "/api/admin/devices",
  "/api/admin/user-by-email",
  "/api/admin/license-by-key",
  "/api/admin/games/catalog",
  "/api/admin/dashboard/users",
  "/api/admin/user-detail",
  "/api/admin/licenses/create",
  "/api/admin/licenses/update",
  "/api/admin/licenses/send-activation",
  "/api/admin/device/deactivate"
]);

export default {
  async fetch(request, env) {
    configuredCorsAllowOrigin = trim(env.CORS_ALLOW_ORIGIN || env.APP_BASE_URL || "");
    const url = new URL(request.url);

    try {
      if (request.method === "OPTIONS") {
        const requestOrigin = trim(request.headers.get("Origin") || "");
        if (configuredCorsAllowOrigin && requestOrigin && requestOrigin !== configuredCorsAllowOrigin) {
          return new Response(
            JSON.stringify({
              error: "cors_origin_not_allowed",
              message: "El origen solicitado no esta autorizado para esta API"
            }),
            {
              status: 403,
              headers: {
                "content-type": "application/json; charset=utf-8"
              }
            }
          );
        }
        return corsResponse();
      }

      let adminContext = null;
      if (ADMIN_PROTECTED_PATHS.has(url.pathname) || url.pathname.startsWith("/api/admin/")) {
        adminContext = await requireAdminUserContext(request, env, url);
        if (adminContext.response) {
          return adminContext.response;
        }
      }

      if (url.pathname === "/api/status") {
        return json({
          status: "online",
          service: "Nisoje Studio API"
        });
      }

      if (url.pathname === "/api/time") {
        return json({
          time: Date.now()
        });
      }

      if (url.pathname === "/api/version") {
        return json({ version: "v4-admin-dashboard" });
      }

      if (url.pathname === "/api/version/latest") {
        const installerUrl = trim(env.INSTALLER_URL) || DEFAULT_INSTALLER_URL;
        const match = installerUrl.match(/\/releases\/download\/(v[\d.]+)\//);
        const latestVersion = match ? match[1] : "v0.1.5";
        return json({
          latest_version: latestVersion,
          installer_url: installerUrl
        });
      }

      if (url.pathname === "/api/db-test") {
        const testToken = trim(request.headers.get("x-db-test-token") || "");
        const expected = trim(env.DB_TEST_TOKEN || "");
        if (expected && testToken !== expected) {
          return json({ error: "db_test_token_required" }, 403);
        }
        const result = await env.DB
          .prepare("SELECT COUNT(*) as total FROM users")
          .first();

        return json({
          database: "connected",
          users_total: result?.total || 0
        });
      }

      if (url.pathname === "/api/register-profile" && request.method === "POST") {
        const profileAuth = await resolveVerifiedFirebaseIdentity(request, env);
        if (profileAuth.response) {
          return profileAuth.response;
        }

        const identity = profileAuth.identity;
        const firebaseUid = trim(identity.firebase_uid);
        const email = trim(identity.email) || null;
        const name = trim(identity.name) || null;

        if (!firebaseUid) {
          return json({ valid: false, message: "firebase_uid es obligatorio" }, 400);
        }

        const user = await upsertUserProfile(env, {
          firebase_uid: firebaseUid,
          email,
          name
        });

        return json({
          valid: true,
          message: "Usuario sincronizado",
          user
        });
      }

      if (url.pathname === "/api/users") {
        const users = await env.DB
          .prepare("SELECT * FROM users ORDER BY id DESC")
          .all();

        return json({
          valid: true,
          users: users.results || []
        });
      }

      if (url.pathname === "/api/create-license" && request.method === "POST") {
        const body = await parseJsonBody(request);
        const userId = Number(body?.user_id);
        const licenseKey = trim(body?.license_key).toUpperCase();
        const expiresAt = trim(body?.expires_at) || null;
        const devicesLimit = normalizeDevicesLimit(body?.devices_limit, 2);

        if (!Number.isFinite(userId) || userId <= 0 || !licenseKey) {
          return json({ valid: false, message: "user_id y license_key son obligatorios" }, 400);
        }

        const duplicate = await env.DB
          .prepare("SELECT id FROM licenses WHERE license_key = ?")
          .bind(licenseKey)
          .first();
        if (duplicate) {
          return json({ valid: false, message: "La clave de licencia ya existe" }, 409);
        }

        await env.DB
          .prepare(
            "INSERT INTO licenses (user_id, license_key, status, devices_limit, expires_at) VALUES (?, ?, 'active', ?, ?)"
          )
          .bind(userId, licenseKey, devicesLimit, expiresAt)
          .run();

        const license = await env.DB
          .prepare("SELECT * FROM licenses WHERE license_key = ?")
          .bind(licenseKey)
          .first();

        return json({
          valid: true,
          message: "Licencia creada",
          license
        });
      }

      if (url.pathname === "/api/licenses") {
        const licenses = await env.DB
          .prepare("SELECT * FROM licenses ORDER BY id DESC")
          .all();

        return json({
          valid: true,
          licenses: licenses.results || []
        });
      }

      if (url.pathname === "/api/license/validate" && request.method === "POST") {
        const body = await parseJsonBody(request);
        const licenseKey = trim(body?.license_key);

        if (!licenseKey) {
          return json({ error: "license_key es obligatorio" }, 400);
        }

        const license = await findLicenseWithUser(env, licenseKey);
        if (!license) {
          return json({ valid: false, message: "Licencia no encontrada" }, 404);
        }

        if (!isLicenseAccessValid(license)) {
          return json({ valid: false, message: "Licencia inactiva o vencida", license }, 403);
        }

        return json({ valid: true, message: "Licencia valida", license });
      }

      if (url.pathname === "/api/license-check") {
        const licenseKey = trim(url.searchParams.get("key")).toUpperCase();
        if (!licenseKey) {
          return json({ error: "Falta key en la URL" }, 400);
        }

        const license = await findLicenseWithUser(env, licenseKey);
        if (!license) {
          return json({ valid: false, message: "Licencia no encontrada" }, 404);
        }

        if (!isLicenseAccessValid(license)) {
          return json({ valid: false, message: "Licencia inactiva o vencida", license }, 403);
        }

        return json({ valid: true, message: "Licencia valida", license });
      }

      if (url.pathname === "/api/license/activate" && request.method === "POST") {
        const body = await parseJsonBody(request);
        const licenseKey = trim(body?.license_key).toUpperCase();
        const deviceId = trim(body?.device_id);
        const deviceName = trim(body?.device_name) || "PC sin nombre";

        if (!licenseKey || !deviceId) {
          return json({ error: "license_key y device_id son obligatorios" }, 400);
        }

        const license = await env.DB
          .prepare("SELECT * FROM licenses WHERE license_key = ?")
          .bind(licenseKey)
          .first();

        if (!license) {
          return json({ valid: false, message: "Licencia no encontrada" }, 404);
        }

        if (!isLicenseAccessValid(license)) {
          return json({ valid: false, message: "Licencia inactiva o vencida" }, 403);
        }

        // Cross-user check: reject if this device_id is already active for another user.
        const conflictDevice = await env.DB
          .prepare("SELECT * FROM devices WHERE device_id = ? AND status = 'active'")
          .bind(deviceId)
          .first();

        if (conflictDevice && Number(conflictDevice.user_id) !== Number(license.user_id)) {
          const conflictUser = await env.DB
            .prepare("SELECT id, email, name FROM users WHERE id = ?")
            .bind(conflictDevice.user_id)
            .first();

          return json({
            valid: false,
            message: "Este dispositivo ya esta registrado en otra cuenta",
            device_conflict: {
              owner_email: conflictUser?.email || null,
              owner_name: conflictUser?.name || null
            }
          }, 403);
        }

        const existingDevice = await env.DB
          .prepare(
            "SELECT * FROM devices WHERE user_id = ? AND device_id = ? AND status = 'active'"
          )
          .bind(license.user_id, deviceId)
          .first();

        if (existingDevice) {
          return json({
            valid: true,
            message: "Dispositivo ya registrado",
            device: existingDevice,
            devices_limit: license.devices_limit
          });
        }

        const usedDevices = await countActiveDevices(env, license.user_id);
        if (usedDevices >= Number(license.devices_limit || 0)) {
          return json({
            valid: false,
            message: "Limite de dispositivos alcanzado",
            devices_used: usedDevices,
            devices_limit: license.devices_limit
          }, 403);
        }

        await env.DB
          .prepare(
            "INSERT INTO devices (user_id, device_name, device_id, status, last_seen_at) VALUES (?, ?, ?, 'active', CURRENT_TIMESTAMP)"
          )
          .bind(license.user_id, deviceName, deviceId)
          .run();

        const newDevice = await env.DB
          .prepare("SELECT * FROM devices WHERE user_id = ? AND device_id = ? ORDER BY id DESC")
          .bind(license.user_id, deviceId)
          .first();

        const updatedDevices = await countActiveDevices(env, license.user_id);
        return json({
          valid: true,
          message: "Dispositivo activado",
          device: newDevice,
          devices_used: updatedDevices,
          devices_limit: license.devices_limit
        });
      }

      if (url.pathname === "/api/devices") {
        const devices = await env.DB
          .prepare("SELECT * FROM devices ORDER BY id DESC")
          .all();

        return json({
          valid: true,
          devices: devices.results || []
        });
      }

      if (url.pathname === "/api/devices-active") {
        const devices = await env.DB
          .prepare("SELECT * FROM devices WHERE status = 'active' ORDER BY id DESC")
          .all();

        return json({
          valid: true,
          devices: devices.results || []
        });
      }

      if (url.pathname === "/api/license/deactivate" && request.method === "POST") {
        const body = await parseJsonBody(request);
        return deactivateDeviceById(env, trim(body?.device_id));
      }

      if (url.pathname === "/api/me/licenses") {
        const auth = await resolveAuthenticatedUserContext(request, env, url, {
          allowFirebaseUidFallback: false,
          requireBearer: true
        });
        if (auth.response) {
          return auth.response;
        }

        const licenses = await loadActiveLicenses(env, auth.user.id);
        const devicesUsed = await countActiveDevices(env, auth.user.id);
        const valid = licenses.length > 0;

        return json({
          valid,
          message: valid ? "Licencias activas encontradas" : "La cuenta no tiene licencias activas vigentes",
          auth_source: auth.auth_source,
          user: {
            id: auth.user.id,
            firebase_uid: auth.user.firebase_uid,
            email: auth.user.email || auth.firebase?.email || null,
            name: auth.user.name || auth.firebase?.name || null,
            role: auth.user.role || null
          },
          devices_used: devicesUsed,
          licenses
        });
      }

      if (url.pathname === "/api/downloads/latest") {
        const auth = await resolveAuthenticatedUserContext(request, env, url, {
          allowFirebaseUidFallback: false,
          requireBearer: true
        });
        if (auth.response) {
          return auth.response;
        }

        return json({
          valid: true,
          items: [
            {
              title: "Panel Live 3.0 para Windows x64",
              description: "Instalador oficial del panel.",
              type: "Windows Setup",
              url: trim(env.INSTALLER_URL) || DEFAULT_INSTALLER_URL,
              updated_at: new Date().toISOString()
            }
          ]
        });
      }

      if (url.pathname === "/api/me/games/catalog") {
        const auth = await resolveAuthenticatedUserContext(request, env, url, {
          allowFirebaseUidFallback: false,
          requireBearer: true
        });
        if (auth.response) {
          return auth.response;
        }

        const activeLicenses = await loadActiveLicenses(env, auth.user.id);
        const catalog = await loadGamesCatalog(env);
        if (catalog.response) {
          return catalog.response;
        }

        const games = [];
        for (const item of catalog.games) {
          if (!isGameAllowedForUser(item, activeLicenses, auth.user)) {
            continue;
          }

          const download = await buildSignedDownloadUrl(
            request,
            env,
            item.package_path,
            auth.user.firebase_uid
          );
          if (!download.ok) {
            return json({
              error: "game_download_sign_failed",
              message: download.message || "No fue posible firmar la descarga"
            }, 500);
          }

          games.push({
            game_id: item.game_id,
            display_name: item.display_name || item.game_id,
            version: item.version || "unknown",
            source: "remote",
            licensed: true,
            sha256: item.sha256,
            download_url: download.url,
            download_url_expires_at: download.expires_at,
            package_path: item.package_path,
            manifest: buildRemoteManifest(item)
          });
        }

        return json({
          ok: true,
          generated_at: catalog.generated_at,
          user: {
            firebase_uid: auth.user.firebase_uid,
            email: auth.user.email || auth.firebase?.email || null
          },
          licenses: activeLicenses.map((item) => ({
            id: item.id,
            license_key: item.license_key,
            status: item.status,
            expires_at: item.expires_at
          })),
          games
        });
      }

      if (url.pathname === DEFAULT_GAME_DOWNLOAD_ROUTE) {
        const signedDownload = await validateSignedDownload(request, url, env);
        if (signedDownload.response) {
          return signedDownload.response;
        }

        const bucket = getGamesCatalogBucket(env);
        if (!bucket) {
          return json({ error: "games_bucket_missing" }, 500);
        }

        const object = await bucket.get(signedDownload.package_path);
        if (!object) {
          return json({ error: "package_not_found" }, 404);
        }

        const fileName = signedDownload.package_path.split("/").pop() || "package.zip";
        const headers = new Headers();
        if (typeof object.writeHttpMetadata === "function") {
          object.writeHttpMetadata(headers);
        }
        headers.set("content-type", headers.get("content-type") || "application/zip");
        headers.set("content-disposition", `attachment; filename="${fileName.replace(/["\r\n]/g, '_')}"`);
        headers.set("cache-control", "private, max-age=60");
        headers.set("x-content-type-options", "nosniff");
        headers.set("referrer-policy", "no-referrer");
        if (object.httpEtag) {
          headers.set("etag", object.httpEtag);
        }
        applyCorsHeaders(headers);

        const peek = await bucket.get(signedDownload.package_path, { onlyIf: {}, range: { length: 2000 } });
        const peekText = peek ? await peek.text() : "";
        const trimmedPeek = peekText.trimStart();
        if (trimmedPeek.startsWith("{") && trimmedPeek.includes('"file"')) {
          const full = await bucket.get(signedDownload.package_path);
          const fullText = await full.text();
          const parsed = JSON.parse(fullText);
          if (parsed && typeof parsed.file === "string") {
            const binaryStr = atob(parsed.file);
            const bytes = new Uint8Array(binaryStr.length);
            for (let i = 0; i < binaryStr.length; i++) bytes[i] = binaryStr.charCodeAt(i);
            return new Response(bytes, { status: 200, headers });
          }
        }
        return new Response(object.body, { status: 200, headers });
      }

      if (url.pathname === "/api/admin/users") {
        const users = await env.DB
          .prepare("SELECT * FROM users ORDER BY id DESC")
          .all();

        return json({
          valid: true,
          users: users.results || []
        });
      }

      if (url.pathname === "/api/admin/licenses") {
        const licenses = await env.DB
          .prepare(
            `SELECT
              licenses.id,
              licenses.user_id,
              licenses.license_key,
              licenses.status,
              licenses.devices_limit,
              licenses.expires_at,
              licenses.created_at,
              users.email,
              users.name
            FROM licenses
            LEFT JOIN users ON licenses.user_id = users.id
            ORDER BY licenses.id DESC`
          )
          .all();

        return json({
          valid: true,
          licenses: licenses.results || []
        });
      }

      if (url.pathname === "/api/admin/devices") {
        const devices = await env.DB
          .prepare(
            `SELECT
              devices.id,
              devices.user_id,
              devices.device_name,
              devices.device_id,
              devices.status,
              devices.last_seen_at,
              devices.created_at,
              users.email,
              users.name
            FROM devices
            LEFT JOIN users ON devices.user_id = users.id
            ORDER BY devices.id DESC`
          )
          .all();

        return json({
          valid: true,
          devices: devices.results || []
        });
      }

      if (url.pathname === "/api/admin/user-by-email") {
        const email = trim(url.searchParams.get("email"));
        if (!email) {
          return json({ error: "Falta email en la URL" }, 400);
        }

        const user = await env.DB
          .prepare("SELECT * FROM users WHERE email = ?")
          .bind(email)
          .first();

        if (!user) {
          return json({ valid: false, message: "Usuario no encontrado" }, 404);
        }

        const detail = await loadAdminUserDetail(env, user.id);
        return json({
          valid: true,
          ...detail
        });
      }

      if (url.pathname === "/api/admin/license-by-key") {
        const licenseKey = trim(url.searchParams.get("key")).toUpperCase();
        if (!licenseKey) {
          return json({ error: "Falta key en la URL" }, 400);
        }

        const license = await env.DB
          .prepare("SELECT * FROM licenses WHERE license_key = ?")
          .bind(licenseKey)
          .first();

        if (!license) {
          return json({ valid: false, message: "Licencia no encontrada" }, 404);
        }

        const detail = await loadAdminUserDetail(env, license.user_id);
        return json({
          valid: true,
          license,
          ...detail
        });
      }

      if (url.pathname === "/api/admin/games/catalog") {
        const catalog = await loadGamesCatalog(env);
        if (catalog.response) {
          return catalog.response;
        }

        return json({
          ok: true,
          generated_at: catalog.generated_at,
          games: catalog.games
        });
      }

      if (url.pathname === "/api/admin/dashboard/users") {
        const query = trim(url.searchParams.get("q")).toLowerCase();
        const users = await loadAdminUsersDashboard(env, query);
        return json({
          valid: true,
          admin: {
            email: adminContext?.user?.email || adminContext?.firebase?.email || null
          },
          metrics: buildAdminDashboardMetrics(users),
          users
        });
      }

      if (url.pathname === "/api/admin/user-detail") {
        const userId = Number(url.searchParams.get("id"));
        if (!Number.isFinite(userId) || userId <= 0) {
          return json({ valid: false, message: "Falta id de usuario valido" }, 400);
        }

        const detail = await loadAdminUserDetail(env, userId);
        if (!detail) {
          return json({ valid: false, message: "Usuario no encontrado" }, 404);
        }

        return json({
          valid: true,
          ...detail
        });
      }

      if (url.pathname === "/api/admin/licenses/create" && request.method === "POST") {
        const body = await parseJsonBody(request);
        const userId = Number(body?.user_id);
        const durationDays = normalizeDurationDays(body?.duration_days);
        const devicesLimit = normalizeDevicesLimit(body?.devices_limit, 2);
        const requestedKey = trim(body?.license_key).toUpperCase();

        if (!Number.isFinite(userId) || userId <= 0) {
          return json({ valid: false, message: "user_id es obligatorio" }, 400);
        }
        if (durationDays <= 0) {
          return json({ valid: false, message: "duration_days debe ser mayor que cero" }, 400);
        }

        const user = await env.DB
          .prepare("SELECT * FROM users WHERE id = ?")
          .bind(userId)
          .first();
        if (!user) {
          return json({ valid: false, message: "Usuario no encontrado" }, 404);
        }

        const licenseKey = requestedKey || await generateUniqueLicenseKey(env);
        const duplicate = await env.DB
          .prepare("SELECT id FROM licenses WHERE license_key = ?")
          .bind(licenseKey)
          .first();
        if (duplicate) {
          return json({ valid: false, message: "La clave de licencia ya existe" }, 409);
        }

        const expiresAt = buildExpiresAtFromDays(durationDays);
        await env.DB
          .prepare(
            "INSERT INTO licenses (user_id, license_key, status, devices_limit, expires_at) VALUES (?, ?, 'active', ?, ?)"
          )
          .bind(userId, licenseKey, devicesLimit, expiresAt)
          .run();

        const license = await env.DB
          .prepare("SELECT * FROM licenses WHERE license_key = ?")
          .bind(licenseKey)
          .first();

        return json({
          valid: true,
          message: "Licencia creada correctamente",
          user,
          license
        });
      }

      if (url.pathname === "/api/admin/licenses/update" && request.method === "POST") {
        const body = await parseJsonBody(request);
        const licenseId = Number(body?.license_id);
        const status = normalizeLicenseStatus(body?.status);
        const durationDays = body?.duration_days === undefined || body?.duration_days === null || body?.duration_days === ""
          ? null
          : normalizeDurationDays(body?.duration_days);
        const devicesLimit = body?.devices_limit === undefined || body?.devices_limit === null || body?.devices_limit === ""
          ? null
          : normalizeDevicesLimit(body?.devices_limit, 2);

        if (!Number.isFinite(licenseId) || licenseId <= 0) {
          return json({ valid: false, message: "license_id es obligatorio" }, 400);
        }
        if (status === null && durationDays === null && devicesLimit === null) {
          return json({ valid: false, message: "No hay cambios para aplicar" }, 400);
        }
        if (durationDays !== null && durationDays <= 0) {
          return json({ valid: false, message: "duration_days debe ser mayor que cero" }, 400);
        }

        const currentLicense = await env.DB
          .prepare("SELECT * FROM licenses WHERE id = ?")
          .bind(licenseId)
          .first();
        if (!currentLicense) {
          return json({ valid: false, message: "Licencia no encontrada" }, 404);
        }

        const nextStatus = status || trim(currentLicense.status).toLowerCase() || "inactive";
        const nextDevicesLimit = devicesLimit ?? Number(currentLicense.devices_limit || 2);
        const nextExpiresAt = durationDays !== null
          ? buildExpiresAtFromDays(durationDays)
          : currentLicense.expires_at;

        await env.DB
          .prepare("UPDATE licenses SET status = ?, devices_limit = ?, expires_at = ? WHERE id = ?")
          .bind(nextStatus, nextDevicesLimit, nextExpiresAt, licenseId)
          .run();

        const license = await env.DB
          .prepare("SELECT * FROM licenses WHERE id = ?")
          .bind(licenseId)
          .first();
        const user = await env.DB
          .prepare("SELECT * FROM users WHERE id = ?")
          .bind(license.user_id)
          .first();

        return json({
          valid: true,
          message: "Licencia actualizada correctamente",
          user,
          license
        });
      }

      if (url.pathname === "/api/admin/licenses/send-activation" && request.method === "POST") {
        const body = await parseJsonBody(request);
        const userId = Number(body?.user_id);
        const licenseId = Number(body?.license_id);
        if (!Number.isFinite(userId) || userId <= 0 || !Number.isFinite(licenseId) || licenseId <= 0) {
          return json({ valid: false, message: "user_id y license_id son obligatorios" }, 400);
        }

        const user = await env.DB
          .prepare("SELECT * FROM users WHERE id = ?")
          .bind(userId)
          .first();
        if (!user) {
          return json({ valid: false, message: "Usuario no encontrado" }, 404);
        }
        if (!trim(user.email)) {
          return json({ valid: false, message: "El usuario no tiene correo registrado" }, 400);
        }

        const license = await env.DB
          .prepare("SELECT * FROM licenses WHERE id = ? AND user_id = ?")
          .bind(licenseId, userId)
          .first();
        if (!license) {
          return json({ valid: false, message: "Licencia no encontrada para este usuario" }, 404);
        }

        const installerUrl = trim(env.INSTALLER_URL) || DEFAULT_INSTALLER_URL;
        const sendResult = await sendActivationEmail(env, { user, license, installerUrl });
        if (!sendResult.ok) {
          return json({
            valid: false,
            message: sendResult.message,
            preview: buildActivationEmailPayload({ user, license, installerUrl }).preview
          }, sendResult.status || 500);
        }

        return json({
          valid: true,
          message: "Correo de activacion enviado correctamente",
          preview: buildActivationEmailPayload({ user, license, installerUrl }).preview
        });
      }

      if (url.pathname === "/api/admin/device/deactivate" && request.method === "POST") {
        const body = await parseJsonBody(request);
        return deactivateDeviceById(env, trim(body?.device_id));
      }

      return new Response("Nisoje Studio API", {
        headers: buildCorsHeaders({
          "content-type": "text/plain; charset=utf-8"
        })
      });
    } catch (error) {
      console.error("Unhandled error in fetch handler:", error);
      return json({
        error: "internal_error",
        message: error instanceof Error ? error.message : "Unexpected error"
      }, 500);
    }
  }
};

function trim(value) {
  if (typeof value !== "string") {
    return "";
  }
  return value.trim();
}

async function parseJsonBody(request) {
  try {
    return await request.json();
  } catch (error) {
    console.error("parseJsonBody: invalid JSON in request", error);
    return null;
  }
}

function normalizeLicenseDateTime(value) {
  const normalized = trim(String(value || ""));
  if (!normalized) {
    return "";
  }

  if (/^\d{4}-\d{2}-\d{2}$/.test(normalized)) {
    return `${normalized}T23:59:59Z`;
  }

  if (/^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$/.test(normalized)) {
    return normalized.replace(" ", "T") + "Z";
  }

  return normalized;
}

function normalizeDurationDays(value) {
  const parsed = Number.parseInt(String(value ?? "").trim(), 10);
  if (!Number.isFinite(parsed)) {
    return 0;
  }
  return Math.max(0, parsed);
}

function normalizeDevicesLimit(value, fallback) {
  const parsed = Number.parseInt(String(value ?? "").trim(), 10);
  if (!Number.isFinite(parsed) || parsed <= 0) {
    return fallback;
  }
  return parsed;
}

function normalizeLicenseStatus(value) {
  const normalized = trim(String(value || "")).toLowerCase();
  if (!normalized) {
    return null;
  }
  if (normalized === "active" || normalized === "inactive") {
    return normalized;
  }
  return null;
}

function isLicenseAccessValid(license) {
  if (!license || trim(license.status).toLowerCase() !== "active") {
    return false;
  }

  const expiresAt = normalizeLicenseDateTime(license.expires_at);
  if (!expiresAt) {
    return true;
  }

  const expiresAtMs = Date.parse(expiresAt);
  if (!Number.isFinite(expiresAtMs)) {
    return false;
  }

  return expiresAtMs >= Date.now();
}

function buildExpiresAtFromDays(durationDays) {
  const expiresAtMs = Date.now() + durationDays * 24 * 60 * 60 * 1000;
  return new Date(expiresAtMs).toISOString();
}

function buildLicenseDurationDays(expiresAt) {
  const normalized = normalizeLicenseDateTime(expiresAt);
  if (!normalized) {
    return null;
  }
  const expiresAtMs = Date.parse(normalized);
  if (!Number.isFinite(expiresAtMs)) {
    return null;
  }
  return Math.max(0, Math.ceil((expiresAtMs - Date.now()) / (24 * 60 * 60 * 1000)));
}

function extractBearerToken(request) {
  const authorization = request.headers.get("Authorization") || "";
  const prefix = "Bearer ";
  if (!authorization.startsWith(prefix)) {
    return "";
  }
  return authorization.slice(prefix.length).trim();
}

function getAdminEmail(env) {
  const email = trim(env.ADMIN_EMAIL || DEFAULT_ADMIN_EMAIL) || DEFAULT_ADMIN_EMAIL;
  if (!email) {
    console.warn("ADMIN_EMAIL not configured — admin routes will deny all access");
  }
  return email;
}

function getGamesCatalogBucket(env) {
  return env.GAMES_CATALOG_BUCKET || env.GAMES_BUCKET || env.GAME_CATALOG_BUCKET || null;
}

function getCatalogObjectKey(env) {
  return trim(env.GAMES_CATALOG_OBJECT || env.GAME_CATALOG_OBJECT || DEFAULT_GAMES_CATALOG_OBJECT)
    || DEFAULT_GAMES_CATALOG_OBJECT;
}

function getDownloadSecret(env) {
  return trim(env.DOWNLOAD_TOKEN_SECRET || env.GAME_DOWNLOAD_SECRET || "");
}

function getDownloadTtlSeconds(env) {
  const raw = Number(env.GAME_DOWNLOAD_TTL_SECONDS || env.DOWNLOAD_URL_TTL_SECONDS || DEFAULT_DOWNLOAD_TTL_SECONDS);
  if (!Number.isFinite(raw) || raw <= 0) {
    return DEFAULT_DOWNLOAD_TTL_SECONDS;
  }
  return Math.floor(raw);
}

async function resolveVerifiedFirebaseIdentity(request, env) {
  const bearerToken = extractBearerToken(request);
  if (!bearerToken) {
    return {
      response: json({
        error: "firebase_token_missing",
        message: "Se requiere Authorization: Bearer para sincronizar el perfil"
      }, 401)
    };
  }

  const firebaseIdentity = await verifyFirebaseIdToken(bearerToken, env);
  if (!firebaseIdentity?.firebase_uid) {
    return {
      response: json({
        error: "firebase_token_invalid",
        message: "Token de Firebase invalido"
      }, 401)
    };
  }

  return {
    identity: firebaseIdentity
  };
}

async function upsertUserProfile(env, input) {
  const firebaseUid = trim(input?.firebase_uid);
  const email = trim(input?.email) || null;
  const name = trim(input?.name) || null;
  if (!firebaseUid) {
    return null;
  }

  const existingUser = await env.DB
    .prepare("SELECT * FROM users WHERE firebase_uid = ?")
    .bind(firebaseUid)
    .first();

  if (existingUser) {
    await env.DB
      .prepare("UPDATE users SET email = ?, name = ? WHERE firebase_uid = ?")
      .bind(email || existingUser.email || null, name || existingUser.name || null, firebaseUid)
      .run();
  } else {
    await env.DB
      .prepare("INSERT INTO users (firebase_uid, email, name) VALUES (?, ?, ?)")
      .bind(firebaseUid, email, name)
      .run();
  }

  return env.DB
    .prepare("SELECT * FROM users WHERE firebase_uid = ?")
    .bind(firebaseUid)
    .first();
}

async function findLicenseWithUser(env, rawKey) {
  const licenseKey = trim(rawKey).toUpperCase();
  return env.DB
    .prepare(
      `SELECT
        licenses.id,
        licenses.user_id,
        licenses.license_key,
        licenses.status,
        licenses.devices_limit,
        licenses.expires_at,
        users.email,
        users.name
      FROM licenses
      LEFT JOIN users ON licenses.user_id = users.id
      WHERE licenses.license_key = ?`
    )
    .bind(licenseKey)
    .first();
}

async function loadUserLicenses(env, userId) {
  const licenses = await env.DB
    .prepare(
      "SELECT id, user_id, license_key, status, devices_limit, expires_at, created_at FROM licenses WHERE user_id = ? ORDER BY id DESC"
    )
    .bind(userId)
    .all();

  return licenses.results || [];
}

async function loadActiveLicenses(env, userId) {
  const licenses = await env.DB
    .prepare(
      `SELECT id, user_id, license_key, status, devices_limit, expires_at, created_at
       FROM licenses
       WHERE user_id = ?
         AND status = 'active'
         AND (expires_at IS NULL OR datetime(expires_at) >= datetime('now'))
       ORDER BY id DESC`
    )
    .bind(userId)
    .all();

  return licenses.results || [];
}

async function countActiveDevices(env, userId) {
  const result = await env.DB
    .prepare("SELECT COUNT(*) as total FROM devices WHERE user_id = ? AND status = 'active'")
    .bind(userId)
    .first();

  return Number(result?.total || 0);
}

async function loadUserDevices(env, userId) {
  const devices = await env.DB
    .prepare("SELECT * FROM devices WHERE user_id = ? ORDER BY id DESC")
    .bind(userId)
    .all();

  return devices.results || [];
}

async function resolveAuthenticatedUserContext(request, env, url, options = {}) {
  const allowFirebaseUidFallback = options.allowFirebaseUidFallback !== false;
  const requireBearer = options.requireBearer === true;
  const bearerToken = extractBearerToken(request);
  let firebaseIdentity = null;

  if (!bearerToken && requireBearer) {
    return {
      response: json({
        error: "firebase_token_missing",
        message: "Se requiere Authorization: Bearer para esta ruta"
      }, 401)
    };
  }

  if (bearerToken) {
    firebaseIdentity = await verifyFirebaseIdToken(bearerToken, env);
    if (!firebaseIdentity) {
      return {
        response: json({ error: "firebase_token_invalid", message: "Token de Firebase invalido" }, 401)
      };
    }
  }

  const fallbackFirebaseUid = allowFirebaseUidFallback
    ? trim(url.searchParams.get("firebase_uid"))
    : "";
  const firebaseUid = firebaseIdentity?.firebase_uid || fallbackFirebaseUid;
  if (!firebaseUid) {
    return {
      response: json({
        error: requireBearer ? "firebase_token_missing" : "firebase_uid_missing",
        message: requireBearer ? "Falta token de Firebase valido" : "Falta identidad del usuario"
      }, requireBearer ? 401 : 400)
    };
  }

  let user = await env.DB
    .prepare("SELECT * FROM users WHERE firebase_uid = ?")
    .bind(firebaseUid)
    .first();

  if (!user && firebaseIdentity?.firebase_uid) {
    user = await upsertUserProfile(env, firebaseIdentity);
  }

  if (!user) {
    return {
      response: json({ valid: false, message: "Usuario no encontrado" }, 404)
    };
  }

  return {
    user,
    firebase: firebaseIdentity,
    auth_source: bearerToken ? "bearer" : "firebase_uid"
  };
}

async function requireAdminUserContext(request, env, url) {
  const auth = await resolveAuthenticatedUserContext(request, env, url, {
    allowFirebaseUidFallback: false,
    requireBearer: true
  });
  if (auth.response) {
    return auth;
  }

  const adminEmail = getAdminEmail(env).toLowerCase();
  const userEmail = trim(auth.firebase?.email || auth.user?.email).toLowerCase();
  if (!userEmail || userEmail !== adminEmail) {
    return {
      response: json({
        valid: false,
        message: "Tu cuenta no tiene permisos admin"
      }, 403)
    };
  }

  return auth;
}

async function verifyFirebaseIdToken(idToken, env) {
  const apiKey = trim(env.FIREBASE_API_KEY || env.FIREBASE_WEB_API_KEY || DEFAULT_FIREBASE_API_KEY);
  if (!apiKey || !idToken) {
    return null;
  }

  const response = await fetch(
    `https://identitytoolkit.googleapis.com/v1/accounts:lookup?key=${encodeURIComponent(apiKey)}`,
    {
      method: "POST",
      headers: {
        "content-type": "application/json"
      },
      body: JSON.stringify({ idToken })
    }
  );

  const payload = await response.json().catch(() => ({}));
  const firstUser = Array.isArray(payload?.users) ? payload.users[0] : null;
  if (!response.ok || !firstUser?.localId) {
    return null;
  }

  return {
    firebase_uid: trim(firstUser.localId),
    email: trim(firstUser.email) || null,
    name: trim(firstUser.displayName) || null
  };
}

async function unwrapR2Json(rawText) {
  const first = JSON.parse(rawText);
  if (first && typeof first.file === "string") {
    const decoded = atob(first.file);
    return JSON.parse(decoded);
  }
  return first;
}

async function loadGamesCatalog(env) {
  const bucket = getGamesCatalogBucket(env);
  if (!bucket) {
    return {
      response: json({ error: "games_bucket_missing", message: "Falta binding R2 del catalogo" }, 500)
    };
  }

  const catalogObject = await bucket.get(getCatalogObjectKey(env));
  if (!catalogObject) {
    return {
      response: json({ error: "games_catalog_not_found", message: "No existe catalog/latest.json en R2" }, 404)
    };
  }

  const rawText = await catalogObject.text();
  let parsed = null;
  try {
    parsed = await unwrapR2Json(rawText);
  } catch {
    return {
      response: json({ error: "games_catalog_invalid", message: "latest.json no es JSON valido" }, 500)
    };
  }

  const games = Array.isArray(parsed?.games) ? parsed.games : [];
  return {
    generated_at: typeof parsed?.generated_at === "string" ? parsed.generated_at : new Date().toISOString(),
    games: games
      .map((item) => normalizeCatalogGame(item))
      .filter((item) => item !== null)
  };
}

function normalizeCatalogGame(input) {
  if (!input || typeof input !== "object") {
    return null;
  }

  const gameId = trim(input.game_id);
  const packagePath = normalizePackagePath(input.package_path);
  const sha256 = trim(String(input.sha256 || "")).toUpperCase();
  if (!gameId || !packagePath || !sha256) {
    return null;
  }

  return {
    game_id: gameId,
    display_name: trim(input.display_name) || gameId,
    version: trim(input.version) || "unknown",
    package_path: packagePath,
    sha256,
    description: trim(input.description) || "Juego live distribuido por Nisoje Studio",
    manifest: input.manifest && typeof input.manifest === "object" ? input.manifest : null,
    required_license_keys: Array.isArray(input.required_license_keys)
      ? input.required_license_keys.map((item) => trim(String(item)).toUpperCase()).filter(Boolean)
      : [],
    required_roles: Array.isArray(input.required_roles)
      ? input.required_roles.map((item) => trim(String(item))).filter(Boolean)
      : []
  };
}

function normalizePackagePath(value) {
  const normalized = trim(String(value || "")).replace(/^\/+/, "");
  if (!normalized || normalized.includes("..") || !normalized.startsWith("catalog/games/")) {
    return "";
  }
  return normalized;
}

function isGameAllowedForUser(game, activeLicenses, user) {
  if (!Array.isArray(activeLicenses) || activeLicenses.length === 0) {
    return false;
  }

  if (Array.isArray(game.required_roles) && game.required_roles.length > 0) {
    const userRole = trim(user?.role);
    if (!userRole || !game.required_roles.includes(userRole)) {
      return false;
    }
  }

  if (Array.isArray(game.required_license_keys) && game.required_license_keys.length > 0) {
    const activeKeys = new Set(activeLicenses.map((item) => trim(item.license_key).toUpperCase()));
    return game.required_license_keys.some((item) => activeKeys.has(item));
  }

  return true;
}

function buildRemoteManifest(game) {
  if (game.manifest && typeof game.manifest === "object") {
    return {
      ...game.manifest,
      gameId: trim(game.manifest.gameId) || game.game_id,
      displayName: trim(game.manifest.displayName) || game.display_name || game.game_id,
      description: trim(game.manifest.description) || game.description
    };
  }

  return {
    gameId: game.game_id,
    displayName: game.display_name || game.game_id,
    description: game.description,
    capabilities: []
  };
}

async function buildSignedDownloadUrl(request, env, packagePath, firebaseUid) {
  const secret = getDownloadSecret(env);
  if (!secret) {
    return { ok: false, message: "DOWNLOAD_TOKEN_SECRET no configurado" };
  }

  const normalizedFirebaseUid = trim(firebaseUid);
  if (!normalizedFirebaseUid) {
    return { ok: false, message: "Falta firebase_uid para firmar la descarga" };
  }

  const expiresAtMs = Date.now() + (getDownloadTtlSeconds(env) * 1000);
  const signature = await signDownloadPayload(packagePath, expiresAtMs, normalizedFirebaseUid, secret);
  const downloadUrl = new URL(request.url);
  downloadUrl.pathname = DEFAULT_GAME_DOWNLOAD_ROUTE;
  downloadUrl.search = "";
  downloadUrl.searchParams.set("path", packagePath);
  downloadUrl.searchParams.set("uid", normalizedFirebaseUid);
  downloadUrl.searchParams.set("expires", String(expiresAtMs));
  downloadUrl.searchParams.set("sig", signature);

  return {
    ok: true,
    url: downloadUrl.toString(),
    expires_at: new Date(expiresAtMs).toISOString()
  };
}

async function validateSignedDownload(request, url, env) {
  const packagePath = normalizePackagePath(url.searchParams.get("path"));
  const firebaseUid = trim(url.searchParams.get("uid"));
  const expiresRaw = trim(url.searchParams.get("expires"));
  const signature = trim(url.searchParams.get("sig")).toUpperCase();
  const secret = getDownloadSecret(env);

  if (!packagePath || !firebaseUid || !expiresRaw || !signature) {
    return {
      response: json({ error: "download_signature_missing" }, 400)
    };
  }

  if (!secret) {
    return {
      response: json({ error: "download_secret_missing" }, 500)
    };
  }

  const expiresAtMs = Number(expiresRaw);
  if (!Number.isFinite(expiresAtMs) || expiresAtMs < Date.now()) {
    return {
      response: json({ error: "download_signature_expired" }, 403)
    };
  }

  const auth = await resolveAuthenticatedUserContext(request, env, url, {
    allowFirebaseUidFallback: false,
    requireBearer: true
  });
  if (auth.response) {
    return auth;
  }

  if (trim(auth.user.firebase_uid) !== firebaseUid) {
    return {
      response: json({ error: "download_user_mismatch" }, 403)
    };
  }

  const expectedSignature = await signDownloadPayload(packagePath, expiresAtMs, firebaseUid, secret);
  if (!timingSafeEqual(signature, expectedSignature)) {
    return {
      response: json({ error: "download_signature_invalid" }, 403)
    };
  }

  return { package_path: packagePath };
}

async function signDownloadPayload(packagePath, expiresAtMs, firebaseUid, secret) {
  const encoder = new TextEncoder();
  const key = await crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign"]
  );

  const payload = `${packagePath}\n${expiresAtMs}\n${trim(firebaseUid)}`;
  const signature = await crypto.subtle.sign("HMAC", key, encoder.encode(payload));
  return bytesToHex(signature).toUpperCase();
}

function bytesToHex(buffer) {
  return Array.from(new Uint8Array(buffer), (byte) => byte.toString(16).padStart(2, "0")).join("");
}

function timingSafeEqual(left, right) {
  if (left.length !== right.length) {
    return false;
  }

  let diff = 0;
  for (let index = 0; index < left.length; index += 1) {
    diff |= left.charCodeAt(index) ^ right.charCodeAt(index);
  }
  return diff === 0;
}

async function loadAdminUsersDashboard(env, query) {
  const usersResult = await env.DB
    .prepare("SELECT id, firebase_uid, email, name, role FROM users ORDER BY id DESC")
    .all();
  const rows = [];

  for (const user of usersResult.results || []) {
    const licenses = await loadUserLicenses(env, user.id);
    const devices = await loadUserDevices(env, user.id);
    const activeLicenses = licenses.filter((item) => isLicenseAccessValid(item));
    const latestLicense = licenses[0] || null;

    const haystack = [
      user.name,
      user.email,
      user.firebase_uid,
      ...licenses.map((item) => item.license_key)
    ]
      .filter(Boolean)
      .join(" ")
      .toLowerCase();

    if (query && !haystack.includes(query)) {
      continue;
    }

    rows.push({
      id: user.id,
      firebase_uid: user.firebase_uid,
      email: user.email,
      name: user.name,
      role: user.role || "user",
      licenses_total: licenses.length,
      active_licenses: activeLicenses.length,
      active_devices: devices.filter((item) => item.status === "active").length,
      latest_license_id: latestLicense?.id || null,
      latest_license_key: latestLicense?.license_key || null,
      latest_license_status: latestLicense?.status || null,
      latest_license_expires_at: latestLicense?.expires_at || null,
      latest_license_days_remaining: latestLicense ? buildLicenseDurationDays(latestLicense.expires_at) : null
    });
  }

  return rows;
}

function buildAdminDashboardMetrics(users) {
  const metrics = {
    total_users: users.length,
    users_with_active_license: 0,
    active_licenses: 0,
    active_devices: 0
  };

  users.forEach((user) => {
    const activeLicenses = Number(user.active_licenses || 0);
    if (activeLicenses > 0) {
      metrics.users_with_active_license += 1;
    }
    metrics.active_licenses += activeLicenses;
    metrics.active_devices += Number(user.active_devices || 0);
  });

  return metrics;
}

async function loadAdminUserDetail(env, userId) {
  const user = await env.DB
    .prepare("SELECT id, firebase_uid, email, name, role FROM users WHERE id = ?")
    .bind(userId)
    .first();
  if (!user) {
    return null;
  }

  const licenses = await loadUserLicenses(env, userId);
  const devices = await loadUserDevices(env, userId);

  return {
    user,
    licenses: licenses.map((license) => ({
      ...license,
      days_remaining: buildLicenseDurationDays(license.expires_at),
      access_valid: isLicenseAccessValid(license)
    })),
    devices
  };
}

async function deactivateDeviceById(env, deviceId) {
  if (!deviceId) {
    return json({ valid: false, message: "device_id es obligatorio" }, 400);
  }

  const existingDevice = await env.DB
    .prepare("SELECT * FROM devices WHERE device_id = ? AND status = 'active'")
    .bind(deviceId)
    .first();

  if (!existingDevice) {
    return json({ valid: false, message: "Dispositivo no encontrado o ya inactivo" }, 404);
  }

  await env.DB
    .prepare("UPDATE devices SET status = 'inactive', last_seen_at = CURRENT_TIMESTAMP WHERE device_id = ?")
    .bind(deviceId)
    .run();

  const updatedDevice = await env.DB
    .prepare("SELECT * FROM devices WHERE device_id = ?")
    .bind(deviceId)
    .first();

  const activeDevices = await countActiveDevices(env, existingDevice.user_id);
  return json({
    valid: true,
    message: "Dispositivo liberado",
    device: updatedDevice,
    devices_used: activeDevices
  });
}

async function generateUniqueLicenseKey(env) {
  for (let attempt = 0; attempt < 8; attempt += 1) {
    const candidate = generateLicenseKeyCandidate();
    const existing = await env.DB
      .prepare("SELECT id FROM licenses WHERE license_key = ?")
      .bind(candidate)
      .first();
    if (!existing) {
      return candidate;
    }
  }
  throw new Error("No fue posible generar una licencia unica");
}

function generateLicenseKeyCandidate() {
  const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  const block = () => Array.from({ length: 4 }, () => alphabet[Math.floor(Math.random() * alphabet.length)]).join("");
  return `NSJ-${block()}-${block()}-${block()}`;
}

function buildActivationEmailPayload({ user, license, installerUrl }) {
  const recipientName = trim(user?.name) || trim(user?.email) || "cliente";
  const licenseKey = trim(license?.license_key) || "Sin licencia";
  const status = trim(license?.status) || "active";
  const expiresAt = trim(license?.expires_at) || "Sin fecha";
  const subject = `Panel Live | Datos de activacion para ${recipientName}`;
  const text = [
    `Hola ${recipientName},`,
    "",
    "Tu acceso de Panel Live ya fue activado.",
    `Correo asociado: ${trim(user?.email) || "Sin correo visible"}`,
    `Licencia: ${licenseKey}`,
    `Estado: ${status}`,
    `Expira: ${expiresAt}`,
    "",
    "Para entrar al panel usa el mismo correo y la contrasena que creaste en la web.",
    "Si no recuerdas tu contrasena, deberas restablecerla.",
    "",
    "Pasos de activacion:",
    "1. Descarga el instalador oficial.",
    "2. Instala Panel Live en tu equipo.",
    "3. Abre el panel e ingresa correo, contrasena y licencia.",
    "",
    `Instalador: ${installerUrl}`,
    "",
    "Si necesitas soporte, responde este correo."
  ].join("\n");

  const html = `
    <div style="font-family:Arial,sans-serif;max-width:640px;margin:0 auto;padding:24px;color:#102030;">
      <h1 style="margin:0 0 16px;font-size:24px;">Panel Live listo para activar</h1>
      <p>Hola <strong>${escapeHtml(recipientName)}</strong>,</p>
      <p>Tu acceso de Panel Live ya fue preparado. Estos son tus datos de activacion:</p>
      <table style="width:100%;border-collapse:collapse;margin:16px 0;">
        <tr><td style="padding:8px 0;font-weight:bold;">Correo</td><td style="padding:8px 0;">${escapeHtml(trim(user?.email) || "Sin correo visible")}</td></tr>
        <tr><td style="padding:8px 0;font-weight:bold;">Licencia</td><td style="padding:8px 0;">${escapeHtml(licenseKey)}</td></tr>
        <tr><td style="padding:8px 0;font-weight:bold;">Estado</td><td style="padding:8px 0;">${escapeHtml(status)}</td></tr>
        <tr><td style="padding:8px 0;font-weight:bold;">Expira</td><td style="padding:8px 0;">${escapeHtml(expiresAt)}</td></tr>
      </table>
      <p>Usa el mismo correo y la contrasena que creaste en la web. Si olvidaste la contrasena, deberas restablecerla.</p>
      <ol>
        <li>Descarga el instalador oficial.</li>
        <li>Instala Panel Live en tu equipo.</li>
        <li>Abre el panel e ingresa correo, contrasena y licencia.</li>
      </ol>
      <p>
        <a href="${escapeHtml(installerUrl)}" style="display:inline-block;padding:12px 18px;background:#ff6a2f;color:white;border-radius:999px;text-decoration:none;font-weight:bold;">
          Descargar instalador
        </a>
      </p>
      <p style="color:#5d6a7a;">Si necesitas soporte, responde este correo.</p>
    </div>
  `;

  return {
    subject,
    text,
    html,
    preview: {
      to: trim(user?.email) || null,
      subject,
      license_key: licenseKey,
      status,
      expires_at: expiresAt,
      installer_url: installerUrl
    }
  };
}

async function sendActivationEmail(env, { user, license, installerUrl }) {
  const apiKey = trim(env.RESEND_API_KEY || "");
  const fromEmail = trim(env.RESEND_FROM_EMAIL || "");
  if (!apiKey || !fromEmail) {
    return {
      ok: false,
      status: 412,
      message: "Faltan RESEND_API_KEY o RESEND_FROM_EMAIL para enviar correos"
    };
  }

  const payload = buildActivationEmailPayload({ user, license, installerUrl });
  const response = await fetch("https://api.resend.com/emails", {
    method: "POST",
    headers: {
      Authorization: `Bearer ${apiKey}`,
      "content-type": "application/json"
    },
    body: JSON.stringify({
      from: fromEmail,
      to: [trim(user.email)],
      subject: payload.subject,
      html: payload.html,
      text: payload.text
    })
  });

  const body = await response.json().catch(() => ({}));
  if (!response.ok) {
    return {
      ok: false,
      status: response.status,
      message: body?.message || "No se pudo enviar el correo de activacion"
    };
  }

  return {
    ok: true,
    status: response.status,
    body
  };
}

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#39;");
}

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: buildCorsHeaders({
      "content-type": "application/json",
      "x-content-type-options": "nosniff",
      "x-frame-options": "DENY",
      "referrer-policy": "no-referrer"
    })
  });
}

function buildCorsHeaders(extraHeaders = {}) {
  const headers = {
    "access-control-allow-methods": CORS_ALLOW_METHODS,
    "access-control-allow-headers": CORS_ALLOW_HEADERS,
    ...extraHeaders
  };
  if (configuredCorsAllowOrigin) {
    headers["access-control-allow-origin"] = configuredCorsAllowOrigin;
    headers.vary = headers.vary ? `${headers.vary}, Origin` : "Origin";
  }
  return headers;
}

function applyCorsHeaders(headers) {
  headers.set("access-control-allow-methods", CORS_ALLOW_METHODS);
  headers.set("access-control-allow-headers", CORS_ALLOW_HEADERS);
  if (configuredCorsAllowOrigin) {
    headers.set("access-control-allow-origin", configuredCorsAllowOrigin);
    headers.set("vary", headers.get("vary") ? `${headers.get("vary")}, Origin` : "Origin");
  } else {
    headers.delete("access-control-allow-origin");
  }
}

function corsResponse() {
  if (!configuredCorsAllowOrigin) {
    return new Response(
      JSON.stringify({
        error: "cors_origin_not_configured",
        message: "Configura CORS_ALLOW_ORIGIN antes de exponer esta API a navegadores"
      }),
      {
        status: 403,
        headers: {
          "content-type": "application/json; charset=utf-8",
          "x-content-type-options": "nosniff"
        }
      }
    );
  }
  return new Response(null, {
    status: 204,
    headers: buildCorsHeaders()
  });
}
