#pragma once

/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <android/binder_status.h>

namespace rdk::hal::indicator {

/**
 * @brief A thin C++ adapter around the Stable AIDL indicator interfaces.
 *
 * This class centralizes:
 * - Service discovery (IIndicatorManager)
 * - Per-indicator handle acquisition (IIndicator)
 * - Capability caching and capability-gated set(state)
 * - Robust Binder error handling patterns
 *
 * Note: This repository contains AIDL definitions and build tooling; this client
 * is intended to be linked into real consumers (controllers, tests, etc.) that
 * have libbinder_ndk available.
 */
class IndicatorClient {
public:
    /**
     * @brief Options controlling how the client connects to the service manager.
     */
    struct Options {
        /**
         * @brief The instance name of the indicator manager service.
         *
         * The repository AIDL declares:
         *   IIndicatorManager.serviceName = "indicator"
         *
         * Stable AIDL services are typically addressed as:
         *   "<descriptor>/<instance>"
         *
         * By default we use instance "default". If a platform publishes a different
         * instance, override this.
         */
        std::string instanceName = "default";

        /**
         * @brief Whether connect() should block waiting for service registration.
         *
         * If false, connect() uses AServiceManager_checkService() and returns
         * std::nullopt when the service is not available yet.
         * If true, connect() uses AServiceManager_waitForService().
         */
        bool waitForService = true;
    };

    /**
     * @brief A lightweight snapshot of an indicator's cached capabilities.
     */
    struct CachedCapabilities {
        std::set<std::string> supportedStates;
    };

    /**
     * @brief Errors returned by the IndicatorClient wrapper.
     *
     * The client does not throw by default; it returns errors explicitly so
     * callers can decide whether failures are fatal or not.
     */
    enum class Error {
        Ok = 0,
        ServiceNotFound,
        BinderError,
        NoIndicators,
        InvalidIndicatorId,
        CapabilitiesUnavailable,
        UnsupportedState,
        SetFailed,
        GetFailed,
    };

    /**
     * @brief A status object containing an error code and a human-readable message.
     */
    struct Status {
        Error error = Error::Ok;
        std::string message;

        /** @brief Convenience true iff status is Ok. */
        bool ok() const { return error == Error::Ok; }
    };

    /**
     * @brief A convenience result type for operations that return both a status and a value.
     */
    template <typename T>
    struct Result {
        Status status;
        std::optional<T> value;
    };

    /**
     * PUBLIC_INTERFACE
     * @brief Connect to the IIndicatorManager service and return a ready IndicatorClient.
     *
     * @param opts Connection options (instance name, wait behavior).
     * @return std::optional<IndicatorClient> Empty if connection could not be established.
     */
    static Result<std::shared_ptr<IndicatorClient>> connect(const Options& opts = Options{});

    /**
     * PUBLIC_INTERFACE
     * @brief Enumerate all indicator IDs offered by the service.
     */
    Result<std::vector<int32_t>> listIndicatorIds();

    /**
     * PUBLIC_INTERFACE
     * @brief Select an indicator ID and acquire its IIndicator handle.
     *
     * This also queries and caches capabilities for that indicator.
     *
     * @param indicatorId Indicator id value (IIndicator::Id.value).
     */
    Status selectIndicator(int32_t indicatorId);

    /**
     * PUBLIC_INTERFACE
     * @brief Return the currently selected indicator id, if any.
     */
    std::optional<int32_t> selectedIndicatorId() const;

    /**
     * PUBLIC_INTERFACE
     * @brief Return cached capabilities for selected indicator (if available).
     */
    Result<CachedCapabilities> getCachedCapabilities() const;

    /**
     * PUBLIC_INTERFACE
     * @brief Query the selected indicator's current state (IIndicator.get()).
     */
    Result<std::string> getState();

    /**
     * PUBLIC_INTERFACE
     * @brief Set selected indicator to the given state.
     *
     * Capability-gated: validates state is present in cached supportedStates.
     * If capabilities have not yet been cached, it tries to query them first.
     *
     * @param state Desired state string.
     * @return Status Ok on successful set(), otherwise a detailed error.
     */
    Status setState(const std::string& state);

private:
    IndicatorClient() = default;

    Status refreshCapabilitiesLocked();
    Status ensureIndicatorSelectedLocked() const;

    static std::string makeServiceName(const std::string& descriptor, const std::string& instance);

    static Status fromBinderStatus(const std::string& context, const ::ndk::ScopedAStatus& st);

private:
    // Forward-declared handle types are stored as shared_ptr<void> to avoid leaking
    // generated AIDL headers into this public header. Implementation includes the headers.
    std::shared_ptr<void> mManager;
    std::shared_ptr<void> mIndicator;

    std::optional<int32_t> mSelectedId;
    std::optional<CachedCapabilities> mCapabilities;
};

} // namespace rdk::hal::indicator
