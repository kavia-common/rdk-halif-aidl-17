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

#include "rdk/hal/indicator/IndicatorClient.h"

// Stable AIDL NDK binder APIs
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <algorithm>
#include <mutex>
#include <sstream>

// Generated headers are expected to exist in the consumer build.
// When using this repository's CMake, AIDL headers are generated under:
//   gen/<target>/<version>/h/<package-path> for C++
// For NDK language, headers land under gen/.../h as well.
// We include the types by their package path.
#include "com/rdk/hal/indicator/Capabilities.h"
#include "com/rdk/hal/indicator/IIndicator.h"
#include "com/rdk/hal/indicator/IIndicatorManager.h"

namespace rdk::hal::indicator {

namespace {
constexpr const char* kDefaultInstance = "default";

using Manager = com::rdk::hal::indicator::IIndicatorManager;
using Indicator = com::rdk::hal::indicator::IIndicator;
using IndicatorId = com::rdk::hal::indicator::IIndicator::Id;
using Capabilities = com::rdk::hal::indicator::Capabilities;

std::string joinStrings(const std::vector<std::string>& v, const char* sep) {
    std::ostringstream oss;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i != 0) {
            oss << sep;
        }
        oss << v[i];
    }
    return oss.str();
}

std::vector<std::string> toVector(const std::set<std::string>& s) {
    return std::vector<std::string>(s.begin(), s.end());
}

/**
 * Map Binder failures to a stable client error surface.
 *
 * For this Indicator interface, we expect:
 * - Status::isOk() for success
 * - EX_ILLEGAL_ARGUMENT for invalid set() argument
 * - Other binder failures treated as transport/remote errors
 */
IndicatorClient::Status mapAStatus(const std::string& context, const ::ndk::ScopedAStatus& st) {
    if (st.isOk()) {
        return {IndicatorClient::Error::Ok, ""};
    }

    std::ostringstream msg;
    msg << context << " failed: " << st.getDescription();

    // Note: getExceptionCode() returns one of binder::Status::Exception codes.
    const int32_t ex = st.getExceptionCode();
    if (ex == EX_ILLEGAL_ARGUMENT) {
        return {IndicatorClient::Error::UnsupportedState, msg.str()};
    }

    return {IndicatorClient::Error::BinderError, msg.str()};
}

} // namespace

std::string IndicatorClient::makeServiceName(const std::string& descriptor, const std::string& instance) {
    // Stable AIDL service instance name convention: "<descriptor>/<instance>"
    // (descriptor is typically the fully qualified interface name).
    return descriptor + "/" + instance;
}

IndicatorClient::Status IndicatorClient::fromBinderStatus(const std::string& context, const ::ndk::ScopedAStatus& st) {
    return mapAStatus(context, st);
}

IndicatorClient::Result<std::shared_ptr<IndicatorClient>> IndicatorClient::connect(const Options& opts) {
    IndicatorClient::Result<std::shared_ptr<IndicatorClient>> out;

    // Ensure binder threadpool exists for incoming binder callbacks (even if none are expected),
    // and to follow typical NDK binder client setup patterns.
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    const std::string instance = opts.instanceName.empty() ? kDefaultInstance : opts.instanceName;
    const std::string serviceName = makeServiceName(Manager::descriptor, instance);

    ndk::SpAIBinder binder;
    if (opts.waitForService) {
        binder = ndk::SpAIBinder(AServiceManager_waitForService(serviceName.c_str()));
    } else {
        binder = ndk::SpAIBinder(AServiceManager_checkService(serviceName.c_str()));
    }

    if (!binder.get()) {
        out.status = {Error::ServiceNotFound,
                      "Indicator manager service not found: '" + serviceName +
                          "'. Ensure the service is registered with servicemanager."};
        out.value = std::nullopt;
        return out;
    }

    auto manager = Manager::fromBinder(binder);
    if (!manager) {
        out.status = {Error::BinderError,
                      "Failed to create IIndicatorManager proxy from binder for service: '" + serviceName + "'"};
        out.value = std::nullopt;
        return out;
    }

    auto client = std::shared_ptr<IndicatorClient>(new IndicatorClient());
    client->mManager = std::static_pointer_cast<void>(manager);

    out.status = {Error::Ok, ""};
    out.value = client;
    return out;
}

IndicatorClient::Result<std::vector<int32_t>> IndicatorClient::listIndicatorIds() {
    Result<std::vector<int32_t>> out;

    if (!mManager) {
        out.status = {Error::BinderError, "IndicatorClient not connected: manager handle is null"};
        return out;
    }

    auto manager = std::static_pointer_cast<Manager>(mManager);

    std::vector<IndicatorId> ids;
    const auto st = manager->getIndicatorIds(&ids);
    const auto mapped = fromBinderStatus("IIndicatorManager.getIndicatorIds", st);
    if (!mapped.ok()) {
        out.status = mapped;
        return out;
    }

    std::vector<int32_t> values;
    values.reserve(ids.size());
    for (const auto& id : ids) {
        values.push_back(id.value);
    }

    if (values.empty()) {
        out.status = {Error::NoIndicators, "Service returned no indicator IDs"};
        out.value = std::vector<int32_t>{};
        return out;
    }

    out.status = {Error::Ok, ""};
    out.value = std::move(values);
    return out;
}

IndicatorClient::Status IndicatorClient::selectIndicator(int32_t indicatorId) {
    if (!mManager) {
        return {Error::BinderError, "IndicatorClient not connected: manager handle is null"};
    }

    auto manager = std::static_pointer_cast<Manager>(mManager);

    IndicatorId id;
    id.value = indicatorId;

    std::shared_ptr<Indicator> indicator;
    const auto st = manager->getIndicator(id, &indicator);
    const auto mapped = fromBinderStatus("IIndicatorManager.getIndicator", st);
    if (!mapped.ok()) {
        return mapped;
    }

    // AIDL declares @nullable return; null is a valid outcome for invalid ID.
    if (!indicator) {
        std::ostringstream msg;
        msg << "IIndicatorManager.getIndicator returned null for indicatorId=" << indicatorId;
        return {Error::InvalidIndicatorId, msg.str()};
    }

    mIndicator = std::static_pointer_cast<void>(indicator);
    mSelectedId = indicatorId;
    mCapabilities.reset();

    // Cache capabilities now to enable capability-gated set().
    return refreshCapabilitiesLocked();
}

std::optional<int32_t> IndicatorClient::selectedIndicatorId() const {
    return mSelectedId;
}

IndicatorClient::Result<IndicatorClient::CachedCapabilities> IndicatorClient::getCachedCapabilities() const {
    Result<CachedCapabilities> out;

    if (!mSelectedId.has_value() || !mIndicator) {
        out.status = {Error::InvalidIndicatorId, "No indicator selected"};
        return out;
    }

    if (!mCapabilities.has_value()) {
        out.status = {Error::CapabilitiesUnavailable, "Capabilities not cached (selectIndicator() not completed)"};
        return out;
    }

    out.status = {Error::Ok, ""};
    out.value = *mCapabilities;
    return out;
}

IndicatorClient::Result<std::string> IndicatorClient::getState() {
    Result<std::string> out;

    const auto stSel = ensureIndicatorSelectedLocked();
    if (!stSel.ok()) {
        out.status = stSel;
        return out;
    }

    auto indicator = std::static_pointer_cast<Indicator>(mIndicator);

    std::string state;
    const auto st = indicator->get(&state);
    const auto mapped = fromBinderStatus("IIndicator.get", st);
    if (!mapped.ok()) {
        out.status = {Error::GetFailed, mapped.message};
        return out;
    }

    out.status = {Error::Ok, ""};
    out.value = std::move(state);
    return out;
}

IndicatorClient::Status IndicatorClient::setState(const std::string& state) {
    const auto stSel = ensureIndicatorSelectedLocked();
    if (!stSel.ok()) {
        return stSel;
    }

    // Ensure we have capabilities cached; if not, try to fetch.
    if (!mCapabilities.has_value()) {
        const auto stCaps = refreshCapabilitiesLocked();
        if (!stCaps.ok()) {
            return stCaps;
        }
    }

    // Capability gate before calling set(): avoids relying on remote-side EX_ILLEGAL_ARGUMENT.
    if (mCapabilities.has_value()) {
        if (mCapabilities->supportedStates.find(state) == mCapabilities->supportedStates.end()) {
            std::ostringstream msg;
            msg << "Requested state '" << state << "' is not in supportedStates=["
                << joinStrings(toVector(mCapabilities->supportedStates), ", ") << "]";
            return {Error::UnsupportedState, msg.str()};
        }
    }

    auto indicator = std::static_pointer_cast<Indicator>(mIndicator);

    bool success = false;
    const auto st = indicator->set(state, &success);
    const auto mapped = fromBinderStatus("IIndicator.set", st);
    if (!mapped.ok()) {
        // If remote returns EX_ILLEGAL_ARGUMENT despite local validation, surface as UnsupportedState.
        if (mapped.error == Error::UnsupportedState) {
            return mapped;
        }
        return {Error::SetFailed, mapped.message};
    }

    if (!success) {
        // As per AIDL: false means unsupported state or setting failed.
        // We already validated supported state, so we treat this as SetFailed.
        std::ostringstream msg;
        msg << "IIndicator.set('" << state << "') returned false";
        return {Error::SetFailed, msg.str()};
    }

    return {Error::Ok, ""};
}

IndicatorClient::Status IndicatorClient::ensureIndicatorSelectedLocked() const {
    if (!mSelectedId.has_value() || !mIndicator) {
        return {Error::InvalidIndicatorId, "No indicator selected; call selectIndicator(indicatorId) first"};
    }
    return {Error::Ok, ""};
}

IndicatorClient::Status IndicatorClient::refreshCapabilitiesLocked() {
    const auto stSel = ensureIndicatorSelectedLocked();
    if (!stSel.ok()) {
        return stSel;
    }

    auto indicator = std::static_pointer_cast<Indicator>(mIndicator);

    Capabilities caps;
    const auto st = indicator->getCapabilities(&caps);
    const auto mapped = fromBinderStatus("IIndicator.getCapabilities", st);
    if (!mapped.ok()) {
        return {Error::CapabilitiesUnavailable, mapped.message};
    }

    CachedCapabilities cached;
    if (caps.supportedStates.has_value()) {
        for (const auto& s : *caps.supportedStates) {
            cached.supportedStates.insert(s);
        }
    }

    if (cached.supportedStates.empty()) {
        return {Error::CapabilitiesUnavailable, "Capabilities.supportedStates empty or missing"};
    }

    mCapabilities = std::move(cached);
    return {Error::Ok, ""};
}

} // namespace rdk::hal::indicator
