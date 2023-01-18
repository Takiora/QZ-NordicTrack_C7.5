﻿#pragma once

#include "Devices/bluetoothdevicetestdata.h"
#include "paferstreadmill.h"

class PafersTreadmillTestData : public BluetoothDeviceTestData {
protected:
    bool configureSettings(DeviceDiscoveryInfo& info, bool enable) const override {
        info.pafers_treadmill = enable;
        return true;
    }

public:
    PafersTreadmillTestData() : BluetoothDeviceTestData("Pafers Treadmill") {
        this->addDeviceName("PAFERS_", comparison::StartsWithIgnoreCase);
    }

    deviceType get_expectedDeviceType() const override { return deviceType::PafersTreadmill; }

    bool get_isExpectedDevice(bluetoothdevice * detectedDevice) const override {
        return dynamic_cast<paferstreadmill*>(detectedDevice)!=nullptr;
    }
};
