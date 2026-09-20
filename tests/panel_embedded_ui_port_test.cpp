// Fase 3 — T3: el puerto local del panel deja de depender de la URL del tunel.
//
// Bug que cubre: el callback de `start_tunnel` escribia la URL efimera del quick
// tunnel en `embedded_ui_url`, y el arranque siguiente derivaba de ahi el puerto
// local. Como una URL de trycloudflare no es loopback, el panel caia siempre a
// 18913 y olvidaba su puerto real.
//
// Este test fija las dos invariantes que lo evitan:
//   1. `embedded_ui_port` es la autoridad del puerto local y sobrevive a los
//      ciclos de arranque.
//   2. `embedded_ui_url` significa solo "URL de la UI embebida": nunca se
//      persiste ni se carga una URL de tunel.

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/panel_config.hpp"
#include "platform/panel_config_storage.hpp"
#include "platform/webview_host.hpp"
#include "test_require.hpp"

namespace {

const char* const kTunnelUrl = "https://bright-river-1234.trycloudflare.com";

std::filesystem::path test_config_path() {
    return std::filesystem::temp_directory_path() / "nlp3_embedded_ui_port_test.json";
}

void write_config(const std::filesystem::path& path, const std::string& body) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    NLP3_TEST_REQUIRE(output.good());
    output << body;
}

std::string read_config(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    NLP3_TEST_REQUIRE(input.good());
    return std::string(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const auto config_path = test_config_path();
    std::filesystem::remove(config_path);

    nlp3::platform::PanelConfigStorage storage;

    // --- 1. Config corrompida por una version anterior: la URL es la del tunel.
    write_config(config_path, std::string("{\n  \"embedded_ui_url\": \"") + kTunnelUrl + "\",\n"
        "  \"embedded_ui_port\": 19400\n}\n");

    nlp3::platform::PanelConfig corrupted{};
    NLP3_TEST_REQUIRE(storage.load_from_file(config_path.string(), corrupted));
    // El puerto real del operador se conserva...
    NLP3_TEST_REQUIRE(corrupted.embedded_ui_port == 19400);
    // ...y la URL vuelve a significar "URL de la UI embebida".
    NLP3_TEST_REQUIRE(corrupted.embedded_ui_url == "http://127.0.0.1:19400/");
    NLP3_TEST_REQUIRE(corrupted.embedded_ui_url.find("trycloudflare") == std::string::npos);

    // El puerto que main.cpp resuelve no es 18913: es el del operador.
    auto target = nlp3::platform::resolve_embedded_ui_target(
        corrupted.embedded_ui_url, corrupted.embedded_ui_port);
    NLP3_TEST_REQUIRE(target.port == 19400);
    NLP3_TEST_REQUIRE(target.url == "http://127.0.0.1:19400/");

    // --- 2. Config antigua sin `embedded_ui_port`: se migra desde la URL.
    write_config(config_path, "{\n  \"embedded_ui_url\": \"http://127.0.0.1:19401/\"\n}\n");

    nlp3::platform::PanelConfig legacy{};
    NLP3_TEST_REQUIRE(storage.load_from_file(config_path.string(), legacy));
    NLP3_TEST_REQUIRE(legacy.embedded_ui_port == 19401);
    NLP3_TEST_REQUIRE(legacy.embedded_ui_url == "http://127.0.0.1:19401/");

    // --- 3. Config antigua Y corrompida: sin puerto guardado cae al default 18913.
    write_config(config_path, std::string("{\n  \"embedded_ui_url\": \"") + kTunnelUrl + "\"\n}\n");

    nlp3::platform::PanelConfig legacy_corrupted{};
    NLP3_TEST_REQUIRE(storage.load_from_file(config_path.string(), legacy_corrupted));
    NLP3_TEST_REQUIRE(legacy_corrupted.embedded_ui_port == 18913);
    NLP3_TEST_REQUIRE(legacy_corrupted.embedded_ui_url == "http://127.0.0.1:18913/");

    // --- 4. Tres arranques seguidos: el puerto resuelto NO cambia y la URL
    //        guardada sigue siendo loopback (nada de tuneles persistidos).
    write_config(config_path, "{\n  \"embedded_ui_url\": \"http://127.0.0.1:19402/\"\n}\n");

    for (int cycle = 0; cycle < 3; ++cycle) {
        nlp3::platform::PanelConfig cycle_config{};
        NLP3_TEST_REQUIRE(storage.load_from_file(config_path.string(), cycle_config));
        NLP3_TEST_REQUIRE(cycle_config.embedded_ui_port == 19402);

        // Lo que hace el panel en el arranque: resolver el destino local.
        const auto resolved = nlp3::platform::resolve_embedded_ui_target(
            cycle_config.embedded_ui_url, cycle_config.embedded_ui_port);
        NLP3_TEST_REQUIRE(resolved.port == 19402);
        NLP3_TEST_REQUIRE(resolved.url == "http://127.0.0.1:19402/");

        // Un arranque guarda la config (por ejemplo al cambiar cualquier ajuste).
        NLP3_TEST_REQUIRE(storage.save_to_file(cycle_config, config_path.string()));
    }

    nlp3::platform::PanelConfig after_cycles{};
    NLP3_TEST_REQUIRE(storage.load_from_file(config_path.string(), after_cycles));
    NLP3_TEST_REQUIRE(after_cycles.embedded_ui_port == 19402);
    NLP3_TEST_REQUIRE(after_cycles.embedded_ui_url == "http://127.0.0.1:19402/");

    // --- 5. El guardado tampoco deja pasar una URL de tunel: si algo la mete en
    //        memoria, el fichero resultante sigue siendo loopback.
    nlp3::platform::PanelConfig dirty{};
    dirty.embedded_ui_url = kTunnelUrl;
    dirty.embedded_ui_port = 19403;
    NLP3_TEST_REQUIRE(storage.save_to_file(dirty, config_path.string()));

    const auto persisted = read_config(config_path);
    NLP3_TEST_REQUIRE(persisted.find("trycloudflare") == std::string::npos);
    NLP3_TEST_REQUIRE(persisted.find("127.0.0.1:19403") != std::string::npos);

    nlp3::platform::PanelConfig reloaded{};
    NLP3_TEST_REQUIRE(storage.load_from_file(config_path.string(), reloaded));
    NLP3_TEST_REQUIRE(reloaded.embedded_ui_url == "http://127.0.0.1:19403/");
    NLP3_TEST_REQUIRE(reloaded.embedded_ui_port == 19403);

    // --- 6. Un puerto 0 nunca se persiste ni se resuelve a 0.
    nlp3::platform::PanelConfig zero_port{};
    zero_port.embedded_ui_port = 0;
    zero_port.embedded_ui_url = "http://127.0.0.1:18913/";
    NLP3_TEST_REQUIRE(storage.save_to_file(zero_port, config_path.string()));

    nlp3::platform::PanelConfig zero_reloaded{};
    NLP3_TEST_REQUIRE(storage.load_from_file(config_path.string(), zero_reloaded));
    NLP3_TEST_REQUIRE(zero_reloaded.embedded_ui_port != 0);
    const auto zero_target = nlp3::platform::resolve_embedded_ui_target(
        zero_reloaded.embedded_ui_url, zero_reloaded.embedded_ui_port);
    NLP3_TEST_REQUIRE(zero_target.port != 0);

    // --- 7. Documenta POR QUE el puerto no puede derivarse de una URL de tunel:
    //        una URL de quick tunnel no es loopback.
    const auto parsed_tunnel = nlp3::platform::parse_embedded_ui_url(kTunnelUrl);
    NLP3_TEST_REQUIRE(parsed_tunnel.valid);
    NLP3_TEST_REQUIRE(!parsed_tunnel.loopback);

    // --- 8. Override explicito: la URL loopback manda sobre el campo del puerto.
    const auto override_target = nlp3::platform::resolve_embedded_ui_target(
        "http://127.0.0.1:19500/", 18913);
    NLP3_TEST_REQUIRE(override_target.port == 19500);

    // --- 9. Una URL no-loopback se ignora y se usa el campo propio.
    const auto tunnel_ignored = nlp3::platform::resolve_embedded_ui_target(kTunnelUrl, 19404);
    NLP3_TEST_REQUIRE(tunnel_ignored.port == 19404);
    NLP3_TEST_REQUIRE(tunnel_ignored.url == "http://127.0.0.1:19404/");

    // --- 10. Una URL loopback con puerto explicito sigue mandando y sincroniza
    //         el campo (no se reescribe la config legitima ya guardada).
    write_config(config_path, "{\n  \"embedded_ui_url\": \"http://127.0.0.1:19991/\"\n}\n");

    nlp3::platform::PanelConfig loopback_explicit{};
    NLP3_TEST_REQUIRE(storage.load_from_file(config_path.string(), loopback_explicit));
    NLP3_TEST_REQUIRE(loopback_explicit.embedded_ui_url == "http://127.0.0.1:19991/");
    NLP3_TEST_REQUIRE(loopback_explicit.embedded_ui_port == 19991);

    std::filesystem::remove(config_path);
    return 0;
}
