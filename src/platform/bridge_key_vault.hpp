#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nlp3::platform {

/// Credencial de proveedor TikTok guardada por el panel.
struct BridgeKeyEntry {
    std::string label{};
    std::string secret{};
    std::int64_t cooldown_until_ms = 0;
    std::int64_t last_used_ms = 0;
    std::size_t failure_count = 0;
};

/// Boveda de credenciales del bridge.
///
/// Los secretos se cifran con DPAPI (CryptProtectData), de modo que solo el
/// usuario de Windows que los guardo puede leerlos: nunca quedan en texto plano
/// en panel_config.json ni en la linea de comandos del runner.
class BridgeKeyVault {
public:
    static std::filesystem::path default_path();
    static std::filesystem::path transient_pool_path();

    /// Carga la boveda. Devuelve false si no existe o no se pudo descifrar
    /// (en ese caso las entradas quedan vacias y el panel sigue funcionando).
    bool load(const std::filesystem::path& path);
    bool save(const std::filesystem::path& path) const;

    const std::vector<BridgeKeyEntry>& entries() const noexcept {
        return entries_;
    }

    bool empty() const noexcept {
        return entries_.empty();
    }

    std::size_t size() const noexcept {
        return entries_.size();
    }

    /// Agrega una credencial. Devuelve false si el label o el secreto estan vacios.
    bool add(std::string label, std::string secret);

    /// Reemplaza una credencial existente (por indice). Devuelve false si el
    /// indice no existe o los datos son invalidos.
    bool replace(std::size_t index, std::string label, std::string secret);

    bool remove_at(std::size_t index);

    /// Marca la credencial como agotada hasta `until_ms` (0 = limpiar).
    bool mark_cooldown(std::size_t index, std::int64_t until_ms);

    /// Primera credencial disponible (fuera de cuarentena); -1 si no hay.
    int first_available(std::int64_t now_ms) const;

    /// Huella corta y no reversible para mostrar en la UI (nunca la key completa).
    static std::string fingerprint(const std::string& secret);

    /// Serializa el pool para el runner: {"keys":[{"label":...,"value":...}]}.
    std::string to_pool_json(std::int64_t now_ms) const;

private:
    std::vector<BridgeKeyEntry> entries_{};
};

} // namespace nlp3::platform
