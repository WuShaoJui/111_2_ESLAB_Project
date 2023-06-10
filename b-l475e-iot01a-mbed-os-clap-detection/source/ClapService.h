/* mbed Microcontroller Library
 * Copyright (c) 2006-2013 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __BLE_CLAP_SERVICE_H__
#define __BLE_CLAP_SERVICE_H__

#include "ble/BLE.h"
#include "ble/Gap.h"
#include "ble/GattServer.h"

class ClapService {
public:
    const static uint16_t CLAP_SERVICE_UUID              = 0xA000;
    const static uint16_t CLAP_STATE_CHARACTERISTIC_UUID = 0xA001;

    ClapService(BLE &_ble, int clapTimeInitial) :
        ble(_ble), claptime(CLAP_SERVICE_UUID, &clapTimeInitial, GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY)
    {
        GattCharacteristic *charTable[] = {&claptime};
        GattService         clapService(ClapService::CLAP_SERVICE_UUID, charTable, sizeof(charTable) / sizeof(GattCharacteristic *));
        ble.gattServer().addService(clapService);
    }

    void updateClapTime(int newTime) {
        ble.gattServer().write(claptime.getValueHandle(), (uint8_t *)&newTime, sizeof(int));
    }

private:
    BLE                              &ble;
    ReadOnlyGattCharacteristic<int>  claptime;
};

#endif /* #ifndef __BLE_CLAP_SERVICE_H__ */
