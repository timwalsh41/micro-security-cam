/**
 * @file web_ui_content.h
 * @brief Embedded single-page web UI served as the firmware default.
 *
 * The UI is a dependency-free single page (HTML + CSS + JS in one
 * document) compiled into the firmware. A future SD-card /data/
 * override can shadow this content per-URL; see
 * docs/SYSTEM_DESIGN.md (Web UI Hosting) for the documented serving
 * order and reserved configuration hook. The override is NOT
 * implemented yet; this module always serves the embedded page.
 */

/**
 * @brief Returns the embedded web UI document.
 * @return Null-terminated HTML source of the single-page UI.
 */
const char* web_ui_html();
