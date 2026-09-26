#ifndef FILE_SERVICE_H
#define FILE_SERVICE_H

#include <cstddef>
#include <cstdint>
#include <functional>

#include "command_handler.h"
#include "logging_service.h"
#include "ini_parser.h"
#include "sd_controller.h"
#include "hal/web_server_hal.h"

/**
 * @file file_service.h
 * @brief SD file browsing, download, and log-viewing service.
 *
 * Owns the LIST_FILES dispatch command (0x40) and the direct HTTP
 * routes for file listing (/files, /logs), file download (/download),
 * and log viewing (/log). All SD access is single-transaction and
 * CS-arbitrated via SdController sync calls.
 *
 * Security policy:
 *  - File routes require a configured web password; when none is set
 *    they refuse with 401 (parity with the config-command policy).
 *  - Paths must be absolute and traversal-free ("..", ".", empty
 *    segments, and backslashes are rejected).
 *  - Listing is allowlisted to the SD root, /logs, and date
 *    directories (/YYYYMMDD).
 *  - Downloads are allowlisted to files under date directories and
 *    log files under /logs; /config.ini is never downloadable (the
 *    config editor reads the live config via GET_CONFIG instead).
 *  - The clip currently being recorded is flagged in listings and
 *    refused for download (its AVI header is unfinalized until close).
 */

/**
 * @class FileService
 * @brief File browser, download, and log-viewing routes and command.
 */
class FileService
{
    public:
        /** Entries requested per listing page, sized to fit the
         *  1280-byte dispatch response cap with typical entry names.
         *  A page may emit fewer entries when the response buffer would
         *  overflow; the response's "next" index then continues the
         *  listing at the first unemitted entry. */
        static constexpr uint16_t PAGE_SIZE = 12;

        /** Scratch buffer size for one serialized listing entry. */
        static constexpr size_t ENTRY_JSON_BUF_SIZE = 176;

        /** Maximum bytes of a log file served by the /log route (tail). */
        static constexpr size_t LOG_TAIL_MAX_BYTES = 16384;

        /** Maximum bytes read per download SD transaction. */
        static constexpr size_t DOWNLOAD_CHUNK_SIZE = 4096;

        /**
         * @brief Constructs the FileService with injected dependencies.
         *
         * @param sd          SD controller for all card access.
         * @param cmd_handler Command handler receiving the LIST_FILES registration.
         * @param log         Logging service for diagnostics.
         */
        FileService(SdController& sd, CommandHandler& cmd_handler,
                    LoggingService& log);

        /**
         * @brief Registers the LIST_FILES command and the HTTP routes.
         *
         * Snapshots the web password for route authentication. Must be
         * called before the web server begins serving requests.
         *
         * @param web          Web server HAL to register routes on.
         * @param web_password Configured web password (empty for none).
         */
        void init(hal::WebServerHal& web, const char* web_password);

        /**
         * @brief Injects the active-clip provider.
         *
         * Called by the composition root so listings can flag and
         * downloads can refuse the clip that is currently being
         * recorded. The provider returns the active clip's absolute
         * path, or an empty string when no clip is being recorded.
         *
         * @param provider Callback returning the active clip path or "".
         */
        void set_active_clip_provider(
            std::function<const char*()> provider);

        // ============================================================
        // LIST_FILES command (0x40)
        // ============================================================

        /**
         * @brief LIST_FILES handler: JSON listing of one directory.
         *
         * Payload format: "<dir>[ <page>]" (page is a decimal entry-page
         * number, default 0; an empty payload lists the SD root).
         * Produces {"dir":...,"page":N,"total":N,"entries":[{"n":...,
         * "s":bytes,"d":is_dir,"r":recording}...],"has_more":bool}.
         *
         * @param payload     Command payload bytes.
         * @param len         Payload length in bytes.
         * @param response    Response buffer (dispatch response cap).
         * @param responseLen Written with the response length in bytes.
         * @return CommandResult for the dispatch framework.
         */
        CommandResult handle_list_files(const uint8_t* payload, size_t len,
                                        uint8_t* response,
                                        size_t& responseLen);

        // ============================================================
        // HTTP routes
        // ============================================================

        /** @brief Handler for GET /files?dir=&start= — JSON listing. */
        void handle_files_request(hal::RequestRef& request);

        /** @brief Handler for GET /download?path= — file transfer. */
        void handle_download_request(hal::RequestRef& request);

        /** @brief Handler for GET /logs?start= — JSON listing of /logs. */
        void handle_logs_request(hal::RequestRef& request);

        /** @brief Handler for GET /log?file=YYYYMMDD.log — log tail. */
        void handle_log_request(hal::RequestRef& request);

        // ============================================================
        // Validation helpers (public for native test coverage)
        // ============================================================

        /**
         * @brief Validates an absolute, traversal-free SD path.
         *
         * Rejects relative paths, empty/"."/“..” segments, backslashes,
         * and trailing slashes.
         */
        static bool is_valid_path(const char* path);

        /**
         * @brief Validates a listing directory against the allowlist.
         *
         * Allowed: the SD root "/", the /logs directory, and date
         * directories (/YYYYMMDD).
         */
        static bool is_allowed_listing_dir(const char* dir);

        /**
         * @brief Validates a download path against the allowlist.
         *
         * Allowed: files directly inside date directories and daily log
         * files under /logs. Root-level files (including /config.ini)
         * and anything deeper are rejected.
         */
        static bool is_allowed_download_path(const char* path);

        /**
         * @brief Validates a daily log file name (YYYYMMDD.log).
         */
        static bool is_log_file_name(const char* name);

        // ============================================================
        // Listing / tail builders (shared by command and routes)
        // ============================================================

        /**
         * @brief Builds the JSON listing page for one directory.
         *
         * Scans the directory once, then serializes the page's entries.
         * Pagination runs over the directory's emittable entries: names
         * too long for the per-entry scratch buffer are excluded from
         * the pagination sequence (they cannot be serialized at all) and
         * reported per response via "skipped_long_names", so "total"
         * and the page math stay consistent. The emitted page stops
         * early (with "more":true and "next" set) when the output
         * buffer would overflow; "next" is the absolute index of the
         * first unemitted emittable entry and serves as the
         * continuation point for the following request.
         *
         * @param dir     Absolute directory path (allowlisted).
         * @param start   Absolute index of the first entry to emit
         *                (clamped to the entry count; values beyond the
         *                end yield an empty page with "more":false).
         * @param buf     Output buffer.
         * @param max_len Output buffer capacity.
         * @return Bytes written, or 0 on directory-listing failure.
         */
        size_t build_listing_json(const char* dir, uint32_t start,
                                  char* buf, size_t max_len);

    private:
        static constexpr const char* SUBSYSTEM = "FILES";

        SdController& m_sd;
        CommandHandler& m_cmd_handler;
        LoggingService& m_log;
        std::function<const char*()> m_active_clip;

        /** Register-time snapshot of the web password for route auth. */
        char m_password[CONFIG_FIELD_MAX_LENGTH]{};

        /**
         * @brief Applies the file-route authentication policy.
         *
         * Refuses with 401 when no web password is configured (file
         * access is not offered unauthenticated); otherwise validates
         * the HTTP Basic credentials and challenges on failure.
         *
         * @return true when the request may proceed.
         */
        bool check_auth(hal::RequestRef& request);
};

#endif // FILE_SERVICE_H
