<!--
MongoDB Document Metadata:
- Original File Path: attachments/NonAVComponent_APIs.xlsx.md
- Operation: write
- Timestamp: 2026-02-05T10:17:03.894168+00:00
- Restored At: 2026-02-23T05:04:11.250177+00:00
- Task ID: cm219d4578
-->

# NonAVComponent_APIs.xlsx (attachment content)

This attachment provides the **exact table content** for `NonAVComponent_APIs.xlsx` (as requested) derived from AIDL interfaces under `rdk-halif-aidl-17/`.

Because this interface only supports writing text, this attachment is provided as **CSV content** that you can paste into Excel (or save as `.csv` and open with Excel), then **Save As → `NonAVComponent_APIs.xlsx`**.

## CSV (Sheet: NonAV components APIs)

```csv
Sl.No,Component,APIs count,"List of APIs (method names only; comma-separated)"
1,HDMI CEC,7,"getState, getProperty, getLogicalAddresses, open, close, registerEventListener, unregisterEventListener"
2,HDMI Input,12,"getCapabilities, getProperty, getState, getEDID, getDefaultEDID, getHDCPCurrentVersion, getHDCPStatus, getSPDInfoFrame, open, close, registerEventListener, unregisterEventListener"
3,HDMI Output,7,"getCapabilities, getProperty, getState, open, close, registerEventListener, unregisterEventListener"
4,Service Manager,0,""
5,Boot,5,"getCapabilities, getBootReason, setBootReason, reboot, getPowerSource"
6,Broadcast,3,"getFrontendIds, getFrontend, openDemux"
7,Common,0,""
8,Deep Sleep,4,"getCapabilities, enterDeepSleep, setWakeUpTimer, getWakeUpTimer"
9,Device Info,2,"getCapabilities, getProperty"
10,Indicator,3,"getCapabilities, set, get"
11,Panel,39,"getCapabilities, getFactoryInterface, setEnabled, getEnabled, setPictureModes, getPictureModes, getDefaultPictureModes, setPQParameters, getPQParameters, getDefaultPQParameters, getPQParameterCapabilities, setRefreshRate, getRefreshRate, setFrameRateMatching, getFrameRateMatching, setVideoSourceOverride, getVideoSourceOverride, getVideoSource, getVideoFormat, getVideoFrameRate, getVideoResolution, set2PointWhiteBalance, get2PointWhiteBalance, setMultiPointWhiteBalance, getMultiPointWhiteBalance, fadeDisplay, setFactoryPanelConfiguration, getFactoryPanelConfiguration, setFactoryWhiteBalanceCalibration, getFactoryWhiteBalanceCalibration, setFactoryGammaTable, getFactoryGammaTable, setFactoryPeakBrightness, getFactoryPeakBrightness, setFactoryLocalDimming, setFactoryLocalDimmingTestMode, setFactoryLocalDimmingPixelCompensation, getFactoryLocalDimmingPixelCompensation, getFactoryBacklightHealth"
12,Sensor,18,"getCapabilities, start, stop, getState, getOperationalMode, getSensitivity, setSensitivity, setAutonomousDuringDeepSleep, isAutonomousDuringDeepSleepEnabled, registerEventListener, unregisterEventListener, setActiveWindows, getActiveWindows, clearActiveWindows, registerEventListener, unregisterEventListener, getCurrentThermalState, getCurrentTemperatures"
```

## Source AIDLs used (paths)

- `rdk-halif-aidl-17/hdmicec/current/com/rdk/hal/hdmicec/IHdmiCec.aidl`
- `rdk-halif-aidl-17/hdmiinput/current/com/rdk/hal/hdmiinput/IHDMIInput.aidl`
- `rdk-halif-aidl-17/hdmioutput/current/com/rdk/hal/hdmioutput/IHDMIOutput.aidl`
- `rdk-halif-aidl-17/boot/current/com/rdk/hal/boot/IBoot.aidl`
- `rdk-halif-aidl-17/broadcast/current/com/rdk/hal/broadcast/IBroadcastManager.aidl`
- `rdk-halif-aidl-17/deepsleep/current/com/rdk/hal/deepsleep/IDeepSleep.aidl`
- `rdk-halif-aidl-17/deviceinfo/current/com/rdk/hal/deviceinfo/IDeviceInfo.aidl`
- `rdk-halif-aidl-17/indicator/current/com/rdk/hal/indicator/IIndicator.aidl`
- `rdk-halif-aidl-17/panel/current/com/rdk/hal/panel/IPanelOutput.aidl`
- `rdk-halif-aidl-17/panel/current/com/rdk/hal/panel/IFactoryPanel.aidl`
- `rdk-halif-aidl-17/sensor/current/com/rdk/hal/sensor/motion/IMotionSensor.aidl`
- `rdk-halif-aidl-17/sensor/current/com/rdk/hal/sensor/thermal/IThermalSensor.aidl`

Notes:
- `Common` is types-only in this repo (no AIDL service interface methods), so APIs count is 0.
- `Service Manager` has no AIDL in this repo (documented as external AOSP C++ interface), so APIs count is 0.
