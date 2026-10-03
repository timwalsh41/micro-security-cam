#ifndef HAL_REQUEST_ARG_STORE_H
#define HAL_REQUEST_ARG_STORE_H

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

/**
 * @file request_arg_store.h
 * @brief Per-request query-parameter value store with stable pointers.
 *
 * A RequestRef::arg() implementation must let a handler hold the value
 * of one parameter while reading another (e.g. key + value, dir +
 * start). A single shared scratch buffer breaks that contract: the
 * second read overwrites the first value, so handlers silently see the
 * wrong parameter. This store keys values by parameter name and keeps
 * each entry's storage stable for the lifetime of the store, so every
 * pointer returned for a name stays valid alongside pointers returned
 * for other names.
 *
 * Platform-agnostic so the storage semantics are testable natively.
 */

namespace hal
{

/**
 * @class RequestArgStore
 * @brief Stores per-name query-parameter values with stable storage.
 *
 * Values longer than MAX_VALUE_LEN are truncated, mirroring the fixed
 * scratch-buffer behavior of a request-scoped implementation.
 * Intended for per-request lifetimes: a value for a given name maps to
 * one request value, so repeated put() calls with identical content
 * keep the previously returned pointer valid.
 */
class RequestArgStore
{
    public:
        /** Maximum stored value length in characters (excluding NUL). */
        static constexpr size_t MAX_VALUE_LEN = 128;

        /**
         * @brief Stores (or refreshes) the value for a parameter name.
         *
         * @param name  Parameter name (nullptr or empty is ignored).
         * @param value Parameter value (nullptr stores an empty value).
         * @return Pointer to the stored value, stable for the store's
         *         lifetime; "" when @p name is unusable.
         */
        const char* put(const char* name, const char* value)
        {
            if (name == nullptr || name[0] == '\0')
            {
                return "";
            }
            if (value == nullptr)
            {
                value = "";
            }

            Entry* entry = find(name);
            if (entry == nullptr)
            {
                m_entries.push_back(std::unique_ptr<Entry>(new Entry()));
                entry = m_entries.back().get();
                entry->name = name;
            }

            size_t len = std::strlen(value);
            if (len > MAX_VALUE_LEN)
            {
                len = MAX_VALUE_LEN;
            }
            entry->value.assign(value, len);
            return entry->value.c_str();
        }

        /**
         * @brief Returns the stored value for a parameter name.
         *
         * @param name Parameter name.
         * @return Pointer to the stored value, or "" when absent.
         */
        const char* get(const char* name) const
        {
            if (name == nullptr)
            {
                return "";
            }
            const Entry* entry = find(name);
            return (entry != nullptr) ? entry->value.c_str() : "";
        }

    private:
        /**
         * @brief One named parameter value.
         *
         * Heap-indirected through unique_ptr so vector reallocation
         * never moves the string objects whose buffers the returned
         * pointers reference.
         */
        struct Entry
        {
            std::string name;
            std::string value;
        };

        Entry* find(const char* name)
        {
            for (const std::unique_ptr<Entry>& entry : m_entries)
            {
                if (entry->name == name)
                {
                    return entry.get();
                }
            }
            return nullptr;
        }

        const Entry* find(const char* name) const
        {
            for (const std::unique_ptr<Entry>& entry : m_entries)
            {
                if (entry->name == name)
                {
                    return entry.get();
                }
            }
            return nullptr;
        }

        std::vector<std::unique_ptr<Entry>> m_entries;
};

} // namespace hal

#endif // HAL_REQUEST_ARG_STORE_H
